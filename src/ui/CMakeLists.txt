# The design tokens, the synced settings and the "?" help buttons: what
# the sync screens (bld_sync_ui, which bld_ui links) share with the rest.
add_library(bld_ui_common STATIC
    theme/Tokens.cpp
    theme/Tokens.h
    theme/LegoColors.cpp
    theme/LegoColors.h
    theme/LegoColorSearch.cpp
    theme/ColorTheme.cpp
    theme/ColorTheme.h
    theme/AppPrefs.cpp
    theme/AppPrefs.h
    help/HelpTexts.cpp
    help/HelpTextStrings.cpp
    help/help.qrc
    help/HelpTexts.h
    help/HelpPages.cpp
    help/HelpPages.h
    help/HelpButton.cpp
    help/HelpButton.h
    help/SourceLinks.cpp
    help/SourceLinks.h
    ConfirmDialog.cpp
    ConfirmDialog.h
    AccessibleNames.cpp
    AccessibleNames.h
)
target_include_directories(bld_ui_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bld_ui_common PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
# Help pages fall back to the source tree's help/ in development builds.
target_compile_definitions(bld_ui_common PRIVATE BLD_HELP_SOURCE_DIR="${CMAKE_SOURCE_DIR}/help")

# Model import (LDraw / Studio / LDD -> a custom part): the app's Tools ›
# Import and the server's headless converter (src/app/bld_import_server.cpp)
# both run exactly this.
add_library(bld_import_pipeline STATIC
    ImportPipeline.cpp
    ImportPipeline.h
)
target_include_directories(bld_import_pipeline PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(bld_import_pipeline
    PUBLIC bld_import bld_rendering bld_parts bld_core Qt6::Core Qt6::Gui Qt6::Widgets)

add_library(bld_ui STATIC
    MainWindow.cpp
    MainWindow.h
    MainWindowBudgetMenu.cpp
    MainWindowFileIO.cpp
    MainWindowImport.cpp
    MainWindowLibrary.cpp
    MainWindowMapMenu.cpp
    MainWindowMenus.cpp
    MainWindowToolsMenu.cpp
    MainWindowViews.cpp
    MapView.cpp
    MapView.h
    PlacementRefusal.h
    MapViewDrag.cpp
    MapViewClipboard.cpp
    MapViewContextMenu.cpp
    MapViewSwitches.cpp
    MapViewRoutes.cpp
    MapViewTrains.cpp
    MapViewRun.cpp
    RunPanel.cpp
    RunPanel.h
    RunSpeeds.h
    MainWindowTrains.cpp
    TrainDialogs.cpp
    TrainDialogs.h
    TrainsPanel.cpp
    TrainsPanel.h
    FitChecksList.cpp
    FitChecksList.h
    MapViewPaint.cpp
    MapViewTouch.cpp
    TouchActionBar.cpp
    TouchActionBar.h
    TouchMode.cpp
    TouchMode.h
    MapViewLoading.cpp
    MapViewInternal.h
    ConnectionSnap.cpp
    ConnectionSnap.h
    SnapFeel.cpp
    SnapFeel.h
    SelectionOverlay.cpp
    SelectionOverlay.h
    ActiveSheet.h
    ActiveSheetButton.cpp
    ActiveSheetButton.h
    LayerPanel.cpp
    LayerPanel.h
    PartsBrowser.cpp
    PartsBrowser.h
    ModulesPanel.cpp
    ModulesPanel.h
    ModuleLookDialog.cpp
    ModuleSetupDialog.cpp
    ModuleSetupText.cpp
    ModuleShareDialog.cpp
    ModuleShareDialog.h
    SharedModulesBar.cpp
    SharedUpdatesDialog.cpp
    SharedModulesBar.h
    SharedModuleWords.cpp
    SharedModuleWords.h
    MainWindowSharedModules.cpp
    SheetChoiceDialog.cpp
    SheetChoiceDialog.h
    ModuleLookDialog.h
    ModuleSetupDialog.h
    ModuleSetupText.h
    ModuleEditBar.cpp
    ModuleEditBar.h
    SetUpdatesBar.cpp
    SetUpdatesBar.h
    MapViewModules.cpp
    ModuleLibraryPanel.cpp
    ModuleThumbnail.cpp
    ModuleLibraryPanel.h
    PartDifferencesDialog.cpp
    PartDifferencesDialog.h
    PartUsagePanel.cpp
    PartUsagePanel.h
    SavedViews.cpp
    SavedViews.h
    SharePictureDialog.cpp
    SharePictureDialog.h
    ViewsPanel.cpp
    RoutesPanel.cpp
    RoutesPanel.h
    ViewsPanel.h
    LoadingCard.cpp
    NoticeArea.cpp
    NotificationViews.cpp
    NotificationSettingsPanel.cpp
    NotificationSettingsPanel.h
    DesktopNotifier.cpp
    DesktopNotifier.h
    NotificationViews.h
    Autosave.cpp
    Autosave.h
    RecoverDialog.cpp
    RecoverDialog.h
    NoticeArea.h
    LoadingCard.h
    LibraryPathsDialog.cpp
    LibraryPathsDialog.h
    BackgroundTask.h
    DownloadCenterDialog.cpp
    DownloadCenterDialog.h
    EditDialogs.cpp
    EditDialogs.h
    ImportPreviewDialog.cpp
    ImportPreviewDialog.h
    FindDialog.cpp
    FindDialog.h
    BudgetDialog.cpp
    BudgetDialog.h
    BudgetSession.cpp
    UpdateCheck.cpp
    UpdateCheck.h
    BudgetSession.h
    VenueDialog.cpp
    VenueDialog.h
    VenueDimensionsDialog.cpp
    VenueDimensionsDialog.h
    VenueLibraryPanel.cpp
    VenueLibraryPanel.h
    venue/VenueDesignerDialog.cpp
    venue/VenueDesignerDialog.h
    venue/VenueDesignerView.cpp
    venue/VenueDesignerView.h
    MainWindowShell.cpp
    SettingsDialog.cpp
    SettingsSections.cpp
    SettingsSections.h
    TranslationCoverage.h
    SettingsDialog.h
    theme/ThemeManager.cpp
    theme/ThemeManager.h
    theme/Icons.cpp
    theme/Icons.h
    theme/PanelHeader.cpp
    theme/PanelHeader.h
    theme/theme.qrc
    theme/AppIcon.cpp
    theme/AppIcon.h
    theme/appicon.qrc
    tours/Tours.cpp
    tours/TourStrings.cpp
    tours/Tours.h
    tours/tours.qrc
    help/ShortcutsDialog.cpp
    help/ShortcutsDialog.h
)

target_link_libraries(bld_ui
    PUBLIC
        bld_ui_common
        bld_core
        bld_saveload
        bld_parts
        bld_rendering
        bld_edit
        bld_import
        bld_import_pipeline
        Qt6::Core
        Qt6::Gui
        Qt6::Network
        Qt6::Widgets
        Qt6::PrintSupport
)

# Live layouts on a collaborative server, and File › Open from Server….
target_sources(bld_ui PRIVATE LiveLayout.cpp LiveLayout.h MainWindowLive.cpp PrefsSync.cpp PrefsSync.h
    MainWindowModules.cpp MainWindowNotifications.cpp ServerLibrary.cpp ServerLibrary.h SaveModuleDialog.cpp SaveModuleDialog.h
    ModuleLibraryDialogs.cpp ModuleLibraryDialogs.h ModuleLibraryMenu.h MainWindowModuleLibrary.cpp
    ServerWindow.cpp ServerWindow.h ClubMembersDialog.cpp ClubMembersDialog.h SendToServerDialogs.cpp SendToServerDialogs.h MainWindowSend.cpp
    SaveSetDialog.cpp SaveSetDialog.h)
target_link_libraries(bld_ui PUBLIC bld_sync_ui)
