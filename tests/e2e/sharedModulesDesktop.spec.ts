// E2E: read-only module sharing with the desktop app in the layout
// (docs/shared-modules.md, phase 1). The desktop side is `bld_sync_driver`,
// the desktop's own SyncSession, lock (edit/SharedLock) and API calls,
// driven over stdin; the web side is the real server and the web editor in
// a browser.
//
//   - Alice (web) shares her module with her club; Bob (web) places it in
//     the club's layout, which moves the layout to doc shape 4;
//   - the desktop opens that layout: it reads shape 4, and sees the module
//     as shared, by Alice, at version 1;
//   - on the desktop a change to its parts or its name is refused with the
//     plain reason, and nothing is sent (the connection stays);
//   - the desktop moves the module as a whole: the server takes it, the
//     web sees it, and the module's placement follows;
//   - Alice saves version 2; the desktop asks the server to put it in
//     (Update): one writer, so the desktop and the web both get the new
//     parts once, where the desktop moved the module to; asking again
//     changes nothing;
//   - the web presses Go back, and the desktop follows;
//   - an older desktop (2.1.4, which reads up to doc shape 3) refuses the
//     layout cleanly and changes nothing (BLD_SYNC_DRIVER_OLD; skipped
//     without it).
//
// This spec lives in the desktop repo. To run it, copy or link it into the
// web repo's apps/web/e2e/test/ and run that one spec against a web stack:
//
//   BLD_SYNC_DRIVER=<build>/src/app/bld_sync_driver \
//   BLD_SYNC_DRIVER_OLD=<2.1.4 build>/src/app/bld_sync_driver \
//   BLD_SYNC_SERVER=http://127.0.0.1:3000 \
//   pnpm --filter @cld/web exec playwright test sharedModulesDesktop
//
// Needs BLD_SYNC_DRIVER (path to the built driver); skipped without it.

import { test, expect, type APIRequestContext, type Page } from '@playwright/test';
import { spawn, type ChildProcessWithoutNullStreams } from 'node:child_process';
import { createInterface } from 'node:readline';
import { signIn } from '../helpers';

const DRIVER = process.env.BLD_SYNC_DRIVER;
const OLD_DRIVER = process.env.BLD_SYNC_DRIVER_OLD;
const SERVER = process.env.BLD_SYNC_SERVER ?? 'http://127.0.0.1:3000';
const ts = Date.now();
const ALICE = `shared-desk-alice-${ts}@example.com`;
const BOB = `shared-desk-bob-${ts}@example.com`;
const PART = 'ts_narrowgauge_straight.8';
const CLUB = `Harbor LUG ${ts % 100000}`;
const NAME = `Harbor crane ${ts % 100000}`;

/** The desktop driver: one command line in, one reply line out. */
class Driver {
  private proc: ChildProcessWithoutNullStreams;
  private lines: string[] = [];
  private waiters: ((line: string) => void)[] = [];
  private stderr = '';

  constructor(path: string, token: string) {
    this.proc = spawn(path, [SERVER], { env: { ...process.env, BLD_SYNC_TOKEN: token } });
    createInterface({ input: this.proc.stdout }).on('line', (l) => {
      const w = this.waiters.shift();
      if (w) w(l);
      else this.lines.push(l);
    });
    this.proc.stderr.on('data', (d: Buffer) => (this.stderr += d.toString()));
  }

  /** Run a command; the whole reply line ("ok …" or "error …"). */
  async say(cmd: string): Promise<string> {
    this.proc.stdin.write(`${cmd}\n`);
    return new Promise<string>((resolve, reject) => {
      const timer = setTimeout(() => reject(new Error(`driver: no reply to "${cmd}"\n${this.stderr}`)), 45_000);
      const done = (l: string) => {
        clearTimeout(timer);
        resolve(l);
      };
      const queued = this.lines.shift();
      if (queued !== undefined) done(queued);
      else this.waiters.push(done);
    });
  }

  /** Run a command; returns what follows "ok", throws on "error". */
  async run(cmd: string): Promise<string> {
    const line = await this.say(cmd);
    if (!line.startsWith('ok')) throw new Error(`driver: "${cmd}" -> ${line}`);
    return line.slice(2).trim();
  }

  async json<T>(cmd: string): Promise<T> {
    return JSON.parse(await this.run(cmd)) as T;
  }

  async close(): Promise<void> {
    if (this.proc.exitCode !== null) return;
    this.proc.stdin.write('quit\n');
    await new Promise((r) => {
      this.proc.once('exit', r);
      setTimeout(r, 3000);
    });
    this.proc.kill();
  }
}

interface Shared {
  id: string;
  moduleId: string;
  version: number;
  by: string;
  name: string;
  members: string[];
  placement: { degrees: number; x: number; y: number } | null;
  review: { from: number; to: number } | null;
}
interface Brick {
  id: string;
  x: number;
  y: number;
  orientation: number;
}

/** An API token for the driver, from the device sign-in the desktop app uses. */
async function desktopToken(page: Page, request: APIRequestContext): Promise<string> {
  const code = (await (
    await request.post('/api/auth/device/code', { data: { client_name: 'Sync driver', scope: 'layouts:read layouts:write' } })
  ).json()) as { device_code: string; user_code: string };
  expect((await page.request.post('/api/auth/device/approve', { data: { user_code: code.user_code } })).ok()).toBe(true);
  const tok = await request.post('/api/auth/device/token', {
    data: { device_code: code.device_code, grant_type: 'urn:ietf:params:oauth:grant-type:device_code' },
  });
  expect(tok.ok()).toBe(true);
  return ((await tok.json()) as { access_token: string }).access_token;
}

function brickCount(page: Page): Promise<number> {
  return page.evaluate(() => {
    type N = { name: () => string; getClassName: () => string };
    const st = (window as unknown as { Konva?: { stages: { find: (f: (n: N) => boolean) => N[] }[] } }).Konva?.stages[0];
    return st ? st.find((n) => n.getClassName() === 'Group' && n.name().startsWith('brick-')).length : 0;
  });
}

async function post(page: Page, url: string, data: object) {
  const res = await page.request.post(url, { data });
  expect(res.ok(), `${url}: ${res.status()} ${await res.text()}`).toBe(true);
  return res.json() as Promise<Record<string, string>>;
}

async function placePart(page: Page): Promise<void> {
  await page.getByPlaceholder('Search parts (name or number)').first().fill('narrow gauge track straight 4 x 16');
  await page.locator(`[title*="(${PART})"]`).first().dblclick();
}

async function saveWithNote(page: Page, note: string): Promise<void> {
  await page.getByLabel('What changed (optional)').fill(note);
  await page.getByRole('button', { name: 'Save module' }).click();
  await expect(page.getByTestId('save-status')).toHaveText('Saved');
}

async function openPanel(page: Page, label: string): Promise<void> {
  await page.getByRole('button', { name: 'Panels', exact: true }).click();
  await page.getByLabel(label, { exact: true }).check();
  await page.mouse.click(400, 400);
}

/** Every brick of the server's layout, from its .bbm export. */
async function serverBricks(page: Page, id: string): Promise<Brick[]> {
  const res = await page.request.get(`/api/layouts/${id}/export.bbm`);
  expect(res.ok()).toBe(true);
  const out: Brick[] = [];
  for (const m of (await res.text()).matchAll(/<Brick id="([^"]+)">([\s\S]*?)<\/Brick>/g)) {
    const field = (tag: string) => Number(new RegExp(`<${tag}>([^<]*)</${tag}>`).exec(m[2]!)![1]);
    out.push({ id: m[1]!, x: field('X'), y: field('Y'), orientation: field('Orientation') });
  }
  return out;
}

test('a shared module with the web and the desktop in one layout', async ({ page, browser, request }) => {
  test.skip(!DRIVER, 'BLD_SYNC_DRIVER (the desktop repo’s bld_sync_driver) is not set');
  test.setTimeout(300_000);

  // Alice makes the module (one track) and a club with Bob in it, and shares the module with the club.
  await signIn(page, ALICE, 'Alice');
  await page.setViewportSize({ width: 1400, height: 1200 });
  const bob = await (await browser.newContext({ viewport: { width: 1400, height: 1200 } })).newPage();
  await signIn(bob, BOB, 'Bob');
  const club = await post(page, '/api/orgs', { name: CLUB });
  const invite = await post(page, `/api/orgs/${club.slug}/invites`, { email: BOB, role: 'member' });
  await post(bob, `/api/org-invites/${invite.token}`, {});

  await page.goto('/');
  await page.getByRole('button', { name: 'New module' }).click();
  const made = page.getByRole('dialog', { name: 'New module' });
  await made.getByLabel('Title').fill(NAME);
  await made.getByRole('button', { name: 'Create and open' }).click();
  await expect(page.locator('canvas').first()).toBeVisible({ timeout: 15000 });
  const moduleUrl = page.url();
  const libraryId = /\/modules\/([^/?#]+)/.exec(moduleUrl)![1]!;
  await placePart(page);
  await expect.poll(() => brickCount(page)).toBe(1);
  await saveWithNote(page, 'One track');
  await post(page, `/api/modules/${libraryId}/club-shares`, { orgSlug: club.slug! });

  // Bob (web) places it in a layout of the club's: the layout is shape 4 from here.
  const layout = await post(bob, '/api/layouts', { title: `Show plan ${ts}`, orgSlug: club.slug! });
  await bob.goto(`/editor/${layout.id}`);
  await expect(bob.locator('canvas').first()).toBeVisible({ timeout: 15000 });
  await openPanel(bob, 'Module library');
  await bob.getByTestId('module-library-tab-shared').click();
  await bob.getByTestId('module-library-row').filter({ hasText: NAME }).getByRole('button', { name: 'Add to layout' }).click();
  await expect.poll(() => brickCount(bob)).toBe(1);
  await openPanel(bob, 'Modules');
  await expect(bob.getByTestId('module-shared-note')).toContainText('Shared by Alice · v1');

  // An older desktop (2.1.4: reads up to shape 3) is told plainly instead of
  // being cut off mid-edit: it never gets in step, and it changes nothing.
  const before = await serverBricks(bob, layout.id!);
  if (OLD_DRIVER) {
    const old = new Driver(OLD_DRIVER, await desktopToken(bob, request));
    try {
      await old.run(`open ${layout.id}`);
      const answer = await old.say('wait-synced 8000');
      expect(answer.startsWith('error'), `the older desktop: ${answer}`).toBe(true);
      expect(await serverBricks(bob, layout.id!)).toEqual(before);
      await expect(bob.getByTestId('module-shared-note')).toContainText('Shared by Alice · v1');
    } finally {
      await old.close();
    }
  }

  // This desktop reads shape 4: Bob opens the same layout there.
  const driver = new Driver(DRIVER!, await desktopToken(bob, request));
  try {
    await driver.run('name Bob on desktop');
    await driver.run(`open ${layout.id}`);
    await driver.run('wait-synced');
    let shared = await driver.json<Shared[]>('shared');
    expect(shared).toHaveLength(1);
    const m = shared[0]!;
    expect(m).toMatchObject({ moduleId: libraryId, version: 1, by: 'Alice', name: NAME });
    expect(m.members).toHaveLength(1);
    expect(m.placement).not.toBeNull();

    // The lock: its name and its parts are its owner's. Nothing is sent; the connection stays.
    const renamed = await driver.say(`module-rename ${m.id} My crane`);
    expect(renamed).toBe(`error locked: “${NAME}” is shared by Alice, so its name is Alice's. You can move it, turn it and change its outline color.`);
    const deleted = await driver.say(`brick-delete ${m.members[0]}`);
    expect(deleted).toContain('error locked:');
    expect(deleted).toContain("so its parts can't be changed here");
    expect(await driver.run('status')).toContain('Synced');
    expect(await serverBricks(bob, layout.id!)).toEqual(before);
    await expect(bob.getByTestId('module-shared-note')).toContainText('Shared by Alice · v1');

    // Moved as a whole: an ordinary edit. The server takes it, and the placement follows.
    await driver.run(`module-move ${m.id} 32 16`);
    await expect
      .poll(async () => (await serverBricks(bob, layout.id!)).map((b) => [b.x - before[0]!.x, b.y - before[0]!.y]), { timeout: 15000 })
      .toEqual([[32, 16]]);
    shared = await driver.json<Shared[]>('shared');
    expect(shared[0]!.placement!.x - m.placement!.x).toBeCloseTo(32, 6);
    expect(shared[0]!.placement!.y - m.placement!.y).toBeCloseTo(16, 6);
    expect(await driver.run('status')).toContain('Synced');

    // Alice saves version 2 (a second track).
    const editor = await page.context().newPage();
    await editor.goto(moduleUrl);
    await expect.poll(() => brickCount(editor), { timeout: 20000 }).toBe(1);
    await placePart(editor);
    await expect.poll(() => brickCount(editor)).toBe(2);
    await saveWithNote(editor, 'longer pier');
    await editor.close();

    // The desktop is told there is one, and may update (Bob edits the club's layouts).
    await expect
      .poll(async () => (await driver.json<{ canUpdate: boolean; modules: { placedId: string; latest: number; latestNote: string; owner: string }[] }>('shared-rows')).modules[0]?.latest, { timeout: 15000 })
      .toBe(2);
    const rows = await driver.json<{ canUpdate: boolean; canDetach: boolean; modules: { placedId: string; state: string; latestNote: string; owner: string }[] }>('shared-rows');
    expect(rows.canUpdate).toBe(true);
    expect(rows.canDetach).toBe(false);
    expect(rows.modules[0]).toMatchObject({ placedId: m.id, state: 'shared', latestNote: 'longer pier', owner: 'Alice' });

    // Update from the desktop: the server puts version 2 in, for the desktop and the web, once.
    expect(await driver.json('shared-apply ' + m.id + ' 2')).toEqual({ changed: true, version: 2, from: 1 });
    await expect.poll(async () => (await driver.json<Shared[]>('shared'))[0]?.version, { timeout: 15000 }).toBe(2);
    shared = await driver.json<Shared[]>('shared');
    expect(shared[0]!.members).toHaveLength(2);
    expect(shared[0]!.review).toEqual({ from: 1, to: 2 });
    await expect.poll(() => brickCount(bob), { timeout: 20000 }).toBe(2);
    await expect(bob.getByTestId('shared-module-updated')).toContainText(`“${NAME}” is now version 2.`);
    // Where the desktop moved it: the first track hasn't gone back.
    const after = await serverBricks(bob, layout.id!);
    expect(after).toHaveLength(2);
    expect(after.some((b) => Math.abs(b.x - (before[0]!.x + 32)) < 0.01 && Math.abs(b.y - (before[0]!.y + 16)) < 0.01)).toBe(true);
    // Pressed again (or by someone else at the same moment): nothing more changes.
    expect(await driver.json('shared-apply ' + m.id + ' 2')).toEqual({ changed: false, version: 2, from: 2 });
    expect(await serverBricks(bob, layout.id!)).toHaveLength(2);
    expect(await driver.run('status')).toContain('Synced');

    // The web goes back to version 1: the desktop follows.
    await bob.getByTestId('shared-module-updated').getByRole('button', { name: 'Go back to version 1' }).click();
    await expect.poll(() => brickCount(bob), { timeout: 20000 }).toBe(1);
    await expect.poll(async () => (await driver.json<Shared[]>('shared'))[0]?.version, { timeout: 15000 }).toBe(1);
    shared = await driver.json<Shared[]>('shared');
    expect(shared[0]!.members).toHaveLength(1);
    expect(await driver.run('status')).toContain('Synced');

    // Make it mine is the layout owner's: a member is told so, in the server's words for the app to show.
    expect(await driver.say(`shared-detach ${m.id}`)).toBe('error 403 only_the_layouts_owner');
  } finally {
    await driver.close();
  }
});
