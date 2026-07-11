#pragma once

namespace CardStack::UiIds {

namespace Menu {
inline constexpr int Startup = 100;
inline constexpr int MainDeck = 101;
inline constexpr int TemplateDesigner = 102;
inline constexpr int ReportDesigner = 10000;
} // namespace Menu

namespace Command {
inline constexpr int FileNew = 2000;
inline constexpr int FileOpen = 2001;
inline constexpr int FileRedefine = 2002;
inline constexpr int FileMerge = 2003;
inline constexpr int FileSave = 2004;
inline constexpr int FileSaveAs = 2005;
inline constexpr int FileClose = 2006;
inline constexpr int FilePrintReport = 2008;
inline constexpr int FilePrinterSetup = 2011;
inline constexpr int FileExit = 2012;
inline constexpr int FileNewReport = 2015;
inline constexpr int FileOpenReport = 2016;
inline constexpr int FileCloseDeck = 2017;
inline constexpr int FileSaveReport = 2018;
inline constexpr int FileSaveReportAs = 2019;

inline constexpr int EditUndo = 2100;
inline constexpr int EditCut = 2101;
inline constexpr int EditCopy = 2102;
inline constexpr int EditPaste = 2103;
inline constexpr int EditSmartPaste = 2104;
inline constexpr int EditClear = 2105;
inline constexpr int EditFirst = EditUndo;
inline constexpr int EditLast = EditClear;

inline constexpr int CardAdd = 2200;
inline constexpr int CardDelete = 2201;
inline constexpr int CardDuplicate = 2202;
inline constexpr int CardUndelete = 2203;
inline constexpr int CardFirst = CardAdd;
inline constexpr int CardLast = CardUndelete;

inline constexpr int ViewCard = 2300;
inline constexpr int ViewTable = 2301;

inline constexpr int SearchFind = 2400;
inline constexpr int SearchFindNext = 2401;
inline constexpr int SearchReplace = 2402;

inline constexpr int PhoneDial = 2450;

inline constexpr int ConfigureDataFont = 2500;
inline constexpr int ConfigureNameFont = 2501;
inline constexpr int ConfigureTextFont = 2502;
inline constexpr int ConfigureIndexFont = 2503;
inline constexpr int ConfigureColors = 2504;
inline constexpr int ConfigurePhoneDialer = 2505;
inline constexpr int ConfigureIndex = 2506;
inline constexpr int ConfigureAddSecurity = 2507;
inline constexpr int ConfigureDeckDescription = 2508;
inline constexpr int ConfigureShowButtonBar = 2509;
inline constexpr int ConfigureEnterWorksLikeTab = 2510;
inline constexpr int ConfigureFontFirst = ConfigureDataFont;
inline constexpr int ConfigureAppearanceLast = ConfigureColors;

inline constexpr int WindowTileVertical = 2700;
inline constexpr int WindowTileHorizontal = 2701;
inline constexpr int WindowCascade = 2702;
inline constexpr int WindowArrangeIcons = 2703;
inline constexpr int WindowCloseAll = 2704;

inline constexpr int RemovedCommand2800 = 2800;
inline constexpr int HelpContents = 2900;
inline constexpr int HelpAbout = 2905;

inline constexpr int NavigateFirstCard = 3301;
inline constexpr int NavigateLastCard = 3303;
inline constexpr int NavigatePreviousWindowful = 4000;
inline constexpr int NavigatePreviousCard = 4001;
inline constexpr int NavigateNextCard = 4002;
inline constexpr int NavigateNextWindowful = 4003;
inline constexpr int NavigateFirst = NavigatePreviousWindowful;
inline constexpr int NavigateLast = NavigateNextWindowful;

inline constexpr int ToolAddText = 4106;
inline constexpr int ToolAddDataBox = 4107;
inline constexpr int ToolAddNotesBox = 4108;
inline constexpr int ToolFrameAttributes = 4109;
inline constexpr int ToolChangeForm = 4150;
inline constexpr int ToolAddSystemData = 4151;
inline constexpr int ToolAddLineOrBox = 4152;
} // namespace Command

namespace Control {
inline constexpr int Ok = 1;
inline constexpr int Cancel = 2;
inline constexpr int Help = 9999;

inline constexpr int NewFileTemplateList = 500;
inline constexpr int NewFileSourceCombo = 501;

inline constexpr int SearchText = 300;
inline constexpr int SearchType = 306;
inline constexpr int SearchAllDataBoxes = 314;
inline constexpr int SearchWholeWord = 304;
inline constexpr int SearchCaseSensitive = 305;
inline constexpr int SearchSoundsLike = 308;
inline constexpr int SearchCompareNone = 310;
inline constexpr int SearchCompareAnd = 311;
inline constexpr int SearchCompareOr = 312;
inline constexpr int SearchSecondText = 320;
inline constexpr int SearchSecondWholeWord = 324;
inline constexpr int SearchSecondCaseSensitive = 325;
inline constexpr int SearchSecondType = 326;
inline constexpr int SearchSecondSoundsLike = 328;
inline constexpr int SearchSecondAllDataBoxes = 334;
inline constexpr int SearchDirectionBeginning = 4500;
inline constexpr int SearchDirectionForward = 4501;
inline constexpr int SearchDirectionBackward = 4502;

inline constexpr int ReplaceCurrentData = 334;
inline constexpr int ReplaceStatus = 102;

inline constexpr int SortReverseLevel1 = 400;
inline constexpr int SortReverseLevel2 = 401;
inline constexpr int SortReverseLevel3 = 402;
inline constexpr int SortFieldLevel1 = 403;
inline constexpr int SortFieldLevel2 = 404;
inline constexpr int SortFieldLevel3 = 405;

inline constexpr int AllCards = 971;
inline constexpr int SelectedCards = 972;

inline constexpr int MergeSourceFile = 950;
inline constexpr int MergeDestinationFile = 951;
inline constexpr int MappingAdd = 952;
inline constexpr int MappingAddAll = 953;
inline constexpr int MappingRemove = 954;
inline constexpr int MappingRemoveAll = 955;
inline constexpr int MappingStatus = 956;
inline constexpr int MappingSourceList = 957;
inline constexpr int MappingDestinationList = 958;

inline constexpr int ExportSourceFile = 970;
inline constexpr int ExportFormat = 900;

inline constexpr int PrintSummary1 = 100;
inline constexpr int PrintSummary2 = 101;
inline constexpr int PrintSummary3 = 102;
inline constexpr int PrintPrinterName = 2000;
inline constexpr int PrintReportName = 2001;
inline constexpr int PrintCopyCount = 2002;
inline constexpr int PrintThisCard = 2004;
inline constexpr int PrintAllCards = 2005;
inline constexpr int PrintSelectedCards = 2006;
inline constexpr int PrintPreview = 2007;
inline constexpr int PrintPrinterSetup = 2008;
inline constexpr int PrintDefineSearch = 2009;

inline constexpr int ReportFormCard = 300;
inline constexpr int ReportFormLabel = 301;
inline constexpr int ReportFormReport = 302;
inline constexpr int ReportFormCustom = 701;
inline constexpr int ReportFormList = 702;

inline constexpr int DefineFormCard = 300;
inline constexpr int DefineFormLabel = 301;
inline constexpr int DefineFormReport = 302;
inline constexpr int DefineFormHeight = 303;
inline constexpr int DefineFormHeightSpin = 304;
inline constexpr int DefineFormWidth = 305;
inline constexpr int DefineFormWidthSpin = 306;
inline constexpr int DefineFormMarginTop = 308;
inline constexpr int DefineFormMarginTopSpin = 309;
inline constexpr int DefineFormMarginLeft = 310;
inline constexpr int DefineFormMarginLeftSpin = 311;
inline constexpr int DefineFormMarginBottom = 312;
inline constexpr int DefineFormMarginBottomSpin = 313;
inline constexpr int DefineFormMarginRight = 314;
inline constexpr int DefineFormMarginRightSpin = 315;
inline constexpr int DefineFormColumnsLabel = 316;
inline constexpr int DefineFormColumns = 317;
inline constexpr int DefineFormColumnsSpin = 318;
inline constexpr int DefineFormRowsLabel = 319;
inline constexpr int DefineFormRows = 320;
inline constexpr int DefineFormRowsSpin = 321;
inline constexpr int DefineFormCountGroup = 323;
inline constexpr int DefineFormPortrait = 325;
inline constexpr int DefineFormLandscape = 326;
inline constexpr int DefineFormHorizontalGutter = 327;
inline constexpr int DefineFormHorizontalGutterSpin = 328;
inline constexpr int DefineFormVerticalGutter = 329;
inline constexpr int DefineFormVerticalGutterSpin = 330;
inline constexpr int DefineFormSample = 2100;
inline constexpr int DefineFormComputedWidth = 4214;
inline constexpr int DefineFormComputedHeight = 4215;

inline constexpr int ReportsList = 402;
inline constexpr int ReportsDelete = 401;
inline constexpr int ReportsModify = 403;
inline constexpr int ReportsNew = 404;
inline constexpr int ReportsAddDefaults = 618;
inline constexpr int ReportsUndoDelete = 903;

inline constexpr int PreviewTitle = 102;
inline constexpr int PreviewPageStatus = 2002;
inline constexpr int PreviewCanvas = 2100;
inline constexpr int PreviewFirstPage = 2103;
inline constexpr int PreviewNextPage = 2104;

inline constexpr int SaveDesignName = 800;
inline constexpr int SaveDesignList = 802;

inline constexpr int FrameText = 4206;
inline constexpr int FrameAlignmentLeft = 4203;
inline constexpr int FrameAlignmentCenter = 4204;
inline constexpr int FrameAlignmentRight = 4205;
inline constexpr int FrameBold = 4303;
inline constexpr int FrameItalic = 4304;
inline constexpr int FrameUnderline = 4305;
inline constexpr int DataFramePrintEntireContents = 4209;
inline constexpr int DataFrameFieldList = 4306;
inline constexpr int LineFrameLineStyle = 4311;
inline constexpr int LineFrameFillPattern = 4312;
inline constexpr int LineFrameBox = 4413;
inline constexpr int LineFrameHorizontal = 4414;
inline constexpr int LineFrameVertical = 4415;
inline constexpr int LineFrameCornerRadius = 4317;

inline constexpr int ColorCustomGrid = 950;
inline constexpr int ColorRoleCombo = 952;
inline constexpr int ColorUseSystem = 953;

inline constexpr int SystemBoxDateCategory = 615;
inline constexpr int SystemBoxNumberCategory = 616;
inline constexpr int SystemBoxSystemCategory = 617;
inline constexpr int SystemBoxLeft = 622;
inline constexpr int SystemBoxCenter = 623;
inline constexpr int SystemBoxRight = 624;
inline constexpr int SystemBoxBold = 627;
inline constexpr int SystemBoxItalic = 628;
inline constexpr int SystemBoxUnderline = 629;
inline constexpr int SystemBoxDateFormats = 715;
inline constexpr int SystemBoxNumberFormats = 716;
inline constexpr int SystemBoxSystemFields = 717;
inline constexpr int SystemBoxPreview = 2100;

inline constexpr int DeckDescriptionFileName = 505;
inline constexpr int DeckDescriptionText = 517;

inline constexpr int SecurityEncryptData = 503;
inline constexpr int SecurityFileName = 505;
inline constexpr int SecurityPassword = 510;

inline constexpr int PhoneOutsideLine = 1407;
inline constexpr int PhoneOutsideLinePrefix = 1408;
inline constexpr int PhoneLongDistance = 1409;
inline constexpr int PhoneLongDistancePrefix = 1410;
inline constexpr int PhoneLogCall = 1411;
inline constexpr int PhoneLocalAreaCode = 1412;
inline constexpr int PhoneNumber = 1413;
inline constexpr int PhoneCardNumbers = 1414;
inline constexpr int PhonePlaceCall = 1415;
inline constexpr int QuickDialDescription = 1420;
inline constexpr int QuickDialNumber = 1421;
inline constexpr int PhoneQuickDials = 1422;
inline constexpr int PhoneQuickDialAdd = 1423;
inline constexpr int PhoneQuickDialModify = 1424;
inline constexpr int PhoneQuickDialDelete = 1425;
} // namespace Control

namespace StringId {
inline constexpr int AllDataBoxes = 419;
inline constexpr int SearchTypeFirst = 442;
inline constexpr int SearchTypeLast = 449;

inline constexpr int NewFromTemplate = 830;
inline constexpr int NewFromScratch = 831;
inline constexpr int NewPatternedAfterTemplate = 832;
inline constexpr int NewPatternedAfterDeck = 833;

inline constexpr int ColorRoleFirst = 840;
inline constexpr int ColorRoleLast = 846;

inline constexpr int SystemBoxDateFormatFirst = 10850;
inline constexpr int SystemBoxDateFormatLast = 10864;
inline constexpr int SystemBoxNumberFormatFirst = 10865;
inline constexpr int SystemBoxNumberFormatLast = 10866;
inline constexpr int SystemBoxFieldFirst = 10867;
inline constexpr int SystemBoxFieldLast = 10870;
inline constexpr int SystemBoxDateTokenFirst = 10880;
inline constexpr int SystemBoxDateTokenLast = 10894;
inline constexpr int SystemBoxNumberTokenFirst = 10895;
inline constexpr int SystemBoxNumberTokenLast = 10896;
inline constexpr int SystemBoxFieldTokenFirst = 10897;
inline constexpr int SystemBoxFieldTokenLast = 10900;
} // namespace StringId

} // namespace CardStack::UiIds
