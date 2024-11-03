// __        ___     _____ ___                  
// \ \      / (_)_ _|_   _/ _ \                 
//  \ \ /\ / /| | '_ \| || (_) |                
//   \ V  V / | | | | | | \__, |                
//  __\_/\_/  |_|_|_|_|_| __/_/     __  ______  
// | __ ) _   _  |__  / |/ _ \ _   _\ \/ /  _ \ 
// |  _ \| | | |   / /| | | | | | | |\  /| |_) |
// | |_) | |_| |  / /_| | |_| | |_| |/  \|  __/ 
// |____/ \__, | /____|_|\___/ \__, /_/\_\_|    
//        |___/                |___/            
//
// Version 1.0
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <dwmapi.h>
#include <fstream>
#include <sstream>
#include <locale>
#include <codecvt>

#pragma comment(lib, "dwmapi.lib")

// Global variables
HHOOK keyboardHook = NULL;
HHOOK mouseHook = NULL; // Mouse hook
std::wstring currentInput = L"";
HWND suggestionWindow = NULL;
std::wstring suggestedWord = L"";

#define WM_TRAYICON (WM_USER + 1)
NOTIFYICONDATA nid = {0};
HMENU hMenu = NULL;

HMENU hSubMenu = NULL; // Submenu for Activate/Deactivate

// N-gram model variables
std::unordered_map<std::wstring, std::unordered_map<std::wstring, int>> ngramModel;
std::wstring previousWord = L"";

// Variable for completion key
UINT completionKey = VK_SPACE; // Default to space key

// Flags
bool isSettingCompletionKey = false;
bool waitingForKey = false;
bool isAutocompleteEnabled = true;

// Ignored words set
std::unordered_set<std::wstring> ignoredWords; // Set for ignored words

// Function declarations
void LoadNGramModel();
void SaveNGramModel();
void LoadUserHistory();
void CreateSuggestionWindow(HINSTANCE hInstance);
void ShowSuggestion();
LRESULT CALLBACK SuggestionWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam);
void InitTrayIcon(HWND hwnd);
void ShowContextMenu(HWND hwnd);
void Cleanup();
bool IsRussianLetter(wchar_t ch);
wchar_t ToLowerRussian(wchar_t ch);
std::wstring GetKeyName(UINT vkCode);
void ShowCompletionKeyDialog(HWND parentHwnd);
LRESULT CALLBACK CompletionKeyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
void RemoveWordFromDictionary(const std::wstring& word);
std::wstring GetWordUnderCaret();
void LoadIgnoredWords();
void SaveIgnoredWords();

// Function to check if character is a Russian letter
bool IsRussianLetter(wchar_t ch) {
    return (ch >= L'А' && ch <= L'я') || ch == L'Ё' || ch == L'ё';
}

// Function to convert Russian character to lowercase
wchar_t ToLowerRussian(wchar_t ch) {
    if (ch >= L'А' && ch <= L'Я') {
        return ch + 32; // Difference between uppercase and lowercase in Unicode Cyrillic
    } else if (ch == L'Ё') {
        return L'ё';
    } else {
        return ch;
    }
}

// Function to get key name from virtual key code
std::wstring GetKeyName(UINT vkCode) {
    UINT scanCode = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
    WCHAR keyName[128];
    if (GetKeyNameTextW(scanCode << 16, keyName, 128) > 0) {
        return keyName;
    } else {
        return L"Unknown";
    }
}

// Function to load N-gram model from corpus.txt
void LoadNGramModel() {
    std::wifstream corpusFile(L"corpus.txt", std::ios::binary);
    if (!corpusFile.is_open()) {
        // Create the file with default content
        std::wofstream newCorpusFile(L"corpus.txt", std::ios::trunc | std::ios::binary);
        if (!newCorpusFile.is_open()) {
            MessageBoxW(NULL, L"Не удалось создать файл corpus.txt!", L"Ошибка", MB_ICONERROR);
            return;
        }

        // Set UTF-16 LE encoding
        newCorpusFile.imbue(std::locale(newCorpusFile.getloc(),
            new std::codecvt_utf16<wchar_t, 0x10ffff, std::codecvt_mode::little_endian>));

        // Write default content
        newCorpusFile << L"привет как дела\n";
        newCorpusFile << L"я сегодня иду в школу\n";
        newCorpusFile << L"спасибо за помощь\n";
        newCorpusFile.close();

        // Open the file again
        corpusFile.open(L"corpus.txt", std::ios::binary);
        if (!corpusFile.is_open()) {
            MessageBoxW(NULL, L"Не удалось открыть файл corpus.txt после создания!", L"Ошибка", MB_ICONERROR);
            return;
        }
    }

    // Set UTF-16 LE encoding
    corpusFile.imbue(std::locale(corpusFile.getloc(),
        new std::codecvt_utf16<wchar_t, 0x10ffff, std::codecvt_mode::little_endian>));

    std::wstring prevWord, word;
    while (corpusFile >> word) {
        // Convert word to lower case
        for (auto& ch : word) {
            ch = ToLowerRussian(ch);
        }

        if (!prevWord.empty()) {
            ngramModel[prevWord][word]++;
        }
        prevWord = word;
    }

    corpusFile.close();
}

// Function to save updated N-gram model to user_history.txt
void SaveNGramModel() {
    std::wofstream historyFile(L"user_history.txt", std::ios::trunc | std::ios::binary);
    if (!historyFile.is_open()) {
        MessageBoxW(NULL, L"Не удалось открыть файл user_history.txt для записи!", L"Ошибка", MB_ICONERROR);
        return;
    }

    // Set UTF-16 LE encoding
    historyFile.imbue(std::locale(historyFile.getloc(),
        new std::codecvt_utf16<wchar_t, 0x10ffff, std::codecvt_mode::little_endian>));

    for (const auto& firstWordPair : ngramModel) {
        const std::wstring& firstWord = firstWordPair.first;
        for (const auto& secondWordPair : firstWordPair.second) {
            const std::wstring& secondWord = secondWordPair.first;
            int count = secondWordPair.second;
            historyFile << firstWord << L" " << secondWord << L" " << count << L"\n";
        }
    }

    historyFile.close();
}

// Function to load user history from user_history.txt
void LoadUserHistory() {
    std::wifstream historyFile(L"user_history.txt", std::ios::binary);
    if (!historyFile.is_open()) {
        // Create the file if it doesn't exist
        std::wofstream newHistoryFile(L"user_history.txt", std::ios::trunc | std::ios::binary);
        if (!newHistoryFile.is_open()) {
            MessageBoxW(NULL, L"Не удалось создать файл user_history.txt!", L"Ошибка", MB_ICONERROR);
            return;
        }
        // Set UTF-16 LE encoding
        newHistoryFile.imbue(std::locale(newHistoryFile.getloc(),
            new std::codecvt_utf16<wchar_t, 0x10ffff, std::codecvt_mode::little_endian>));
        newHistoryFile.close();
        return;
    }

    // Set UTF-16 LE encoding
    historyFile.imbue(std::locale(historyFile.getloc(),
        new std::codecvt_utf16<wchar_t, 0x10ffff, std::codecvt_mode::little_endian>));

    std::wstring firstWord, secondWord;
    int count;
    while (historyFile >> firstWord >> secondWord >> count) {
        ngramModel[firstWord][secondWord] += count;
    }

    historyFile.close();
}

// Function to load ignored words from ignored_words.txt
void LoadIgnoredWords() {
    std::wifstream ignoredFile(L"ignored_words.txt", std::ios::binary);
    if (!ignoredFile.is_open()) {
        // Create the file if it doesn't exist
        std::wofstream newIgnoredFile(L"ignored_words.txt", std::ios::trunc | std::ios::binary);
        if (!newIgnoredFile.is_open()) {
            MessageBoxW(NULL, L"Не удалось создать файл ignored_words.txt!", L"Ошибка", MB_ICONERROR);
            return;
        }
        // Set UTF-16 LE encoding
        newIgnoredFile.imbue(std::locale(newIgnoredFile.getloc(),
            new std::codecvt_utf16<wchar_t, 0x10ffff, std::codecvt_mode::little_endian>));
        newIgnoredFile.close();
        return;
    }

    // Set UTF-16 LE encoding
    ignoredFile.imbue(std::locale(ignoredFile.getloc(),
        new std::codecvt_utf16<wchar_t, 0x10ffff, std::codecvt_mode::little_endian>));

    std::wstring word;
    while (ignoredFile >> word) {
        ignoredWords.insert(word);
    }

    ignoredFile.close();
}

// Function to save ignored words to ignored_words.txt
void SaveIgnoredWords() {
    std::wofstream ignoredFile(L"ignored_words.txt", std::ios::trunc | std::ios::binary);
    if (!ignoredFile.is_open()) {
        MessageBoxW(NULL, L"Не удалось открыть файл ignored_words.txt для записи!", L"Ошибка", MB_ICONERROR);
        return;
    }

    // Set UTF-16 LE encoding
    ignoredFile.imbue(std::locale(ignoredFile.getloc(),
        new std::codecvt_utf16<wchar_t, 0x10ffff, std::codecvt_mode::little_endian>));

    for (const auto& word : ignoredWords) {
        ignoredFile << word << L"\n";
    }

    ignoredFile.close();
}

// Function to remove a word from the dictionary
void RemoveWordFromDictionary(const std::wstring& word) {
    // Remove the word from all entries in the n-gram model
    for (auto& firstWordPair : ngramModel) {
        firstWordPair.second.erase(word);
    }
    // Also remove any entries where this word is a first word
    ngramModel.erase(word);

    // Add word to ignored words
    ignoredWords.insert(word);

    // Save updated N-gram model and ignored words
    SaveNGramModel();
    SaveIgnoredWords();
}

// Function to get the word under the caret
std::wstring GetWordUnderCaret() {
    HWND hWnd = GetForegroundWindow();
    if (!hWnd) return L"";

    DWORD threadId = GetWindowThreadProcessId(hWnd, NULL);
    GUITHREADINFO guiThreadInfo = {0};
    guiThreadInfo.cbSize = sizeof(GUITHREADINFO);
    if (!GetGUIThreadInfo(threadId, &guiThreadInfo)) {
        return L"";
    }

    HWND hFocus = guiThreadInfo.hwndFocus;
    if (!hFocus) return L"";

    // Assuming the control is an edit control
    int length = GetWindowTextLengthW(hFocus);
    if (length == 0) return L"";

    // Get the text from the control
    std::wstring text(length + 1, L'\0');
    GetWindowTextW(hFocus, &text[0], length + 1);
    text.resize(length);

    // Get the caret position
    int caretPos = LOWORD(SendMessageW(hFocus, EM_GETSEL, 0, 0));
    if (caretPos > length) caretPos = length;

    // Find the start and end of the word under the caret
    int start = caretPos;
    while (start > 0 && IsRussianLetter(text[start - 1])) {
        start--;
    }
    int end = caretPos;
    while (end < length && IsRussianLetter(text[end])) {
        end++;
    }

    if (start == end) return L"";

    // Extract the word under the caret
    std::wstring wordUnderCaret = text.substr(start, end - start);

    // Convert to lowercase
    for (auto& ch : wordUnderCaret) {
        ch = ToLowerRussian(ch);
    }

    return wordUnderCaret;
}

// Window procedure for suggestion window
LRESULT CALLBACK SuggestionWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        // Set modern font and color
        HFONT hFont = CreateFontW(16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                                  RUSSIAN_CHARSET, OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,
                                  CLEARTYPE_QUALITY, VARIABLE_PITCH, L"Arial");
        SelectObject(hdc, hFont);

        SetBkMode(hdc, TRANSPARENT);
        SetTextColor(hdc, RGB(255, 255, 255));

        RECT rect;
        GetClientRect(hwnd, &rect);

        // Draw rounded rectangle background
        HBRUSH hBrush = CreateSolidBrush(RGB(50, 50, 50));
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, hBrush);
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(50, 50, 50));
        HPEN oldPen = (HPEN)SelectObject(hdc, hPen);

        // Fill rectangle without borders
        FillRect(hdc, &rect, hBrush);

        // Draw the suggested text
        DrawTextW(hdc, suggestedWord.c_str(), -1, &rect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);

        // Cleanup
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(hBrush);
        DeleteObject(hPen);
        DeleteObject(hFont);

        EndPaint(hwnd, &ps);
        break;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            ShowContextMenu(hwnd);
        }
        break;
    case WM_RBUTTONDOWN: {
        // Show context menu to delete or ignore word
        POINT pt;
        GetCursorPos(&pt);
        HMENU hContextMenu = CreatePopupMenu();
        AppendMenuW(hContextMenu, MF_STRING, 2001, L"Удалить");
        AppendMenuW(hContextMenu, MF_STRING, 2002, L"Игнорировать");

        SetForegroundWindow(hwnd); // Necessary to ensure the menu closes correctly
        UINT clicked = TrackPopupMenu(
            hContextMenu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON,
            pt.x,
            pt.y,
            0,
            hwnd,
            NULL
        );
        DestroyMenu(hContextMenu);

        // Retrieve the suggested word directly from the window text
        wchar_t buffer[256];
        GetWindowTextW(hwnd, buffer, 256);
        std::wstring word(buffer);

        if (clicked == 2001) {
            // Remove the word from the dictionary
            RemoveWordFromDictionary(word);
            suggestedWord.clear();
            ShowSuggestion();
        } else if (clicked == 2002) {
            // Add word to ignored words
            ignoredWords.insert(word);
            SaveIgnoredWords();
            suggestedWord.clear();
            ShowSuggestion();
        }
        break;
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// Function to create a tooltip-like window for suggestions
void CreateSuggestionWindow(HINSTANCE hInstance) {
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = SuggestionWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"SuggestionWindowClass";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = NULL; // Make background transparent

    RegisterClassW(&wc);

    suggestionWindow = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        wc.lpszClassName,
        NULL,
        WS_POPUP,
        CW_USEDEFAULT, CW_USEDEFAULT, 150, 30,
        NULL, NULL, hInstance, NULL
    );

    // Make window semi-transparent
    SetLayeredWindowAttributes(suggestionWindow, 0, (BYTE)(200), LWA_ALPHA);

    // Add drop shadow effect
    MARGINS margins = {0, 0, 0, 0};
    DwmExtendFrameIntoClientArea(suggestionWindow, &margins);
}

// Function to show suggestion near the caret as a tooltip
void ShowSuggestion() {
    if (suggestedWord.empty()) {
        ShowWindow(suggestionWindow, SW_HIDE);
        return;
    }

    // Set the suggested word as the window text
    SetWindowTextW(suggestionWindow, suggestedWord.c_str());

    // Get GUI thread info
    GUITHREADINFO guiThreadInfo = {0};
    guiThreadInfo.cbSize = sizeof(GUITHREADINFO);
    if (GetGUIThreadInfo(0, &guiThreadInfo) && guiThreadInfo.hwndCaret) {
        // Get caret rectangle in client coordinates
        RECT caretRect = guiThreadInfo.rcCaret;
        POINT caretPos = { caretRect.left, caretRect.bottom };
        // Convert to screen coordinates
        ClientToScreen(guiThreadInfo.hwndCaret, &caretPos);

        // Set position near the caret
        int windowWidth = 150;
        int windowHeight = 30;
        int posX = caretPos.x;
        int posY = caretPos.y + 5; // Slight offset below the caret

        // Adjust position if window goes off screen
        RECT desktop;
        const HWND hDesktop = GetDesktopWindow();
        GetWindowRect(hDesktop, &desktop);

        if (posX + windowWidth > desktop.right) {
            posX = desktop.right - windowWidth - 5;
        }
        if (posY + windowHeight > desktop.bottom) {
            posY = desktop.bottom - windowHeight - 5;
        }

        // Move and size the suggestion window
        SetWindowPos(suggestionWindow, HWND_TOPMOST, posX, posY, windowWidth, windowHeight, SWP_SHOWWINDOW | SWP_NOACTIVATE);

        // Redraw the window
        InvalidateRect(suggestionWindow, NULL, TRUE);
    } else {
        // Fallback to cursor position if caret position is not available
        POINT cursorPos;
        GetCursorPos(&cursorPos);

        int windowWidth = 150;
        int windowHeight = 30;
        int posX = cursorPos.x + 10;
        int posY = cursorPos.y + 10;

        // Adjust position if window goes off screen
        RECT desktop;
        const HWND hDesktop = GetDesktopWindow();
        GetWindowRect(hDesktop, &desktop);

        if (posX + windowWidth > desktop.right) {
            posX = desktop.right - windowWidth - 5;
        }
        if (posY + windowHeight > desktop.bottom) {
            posY = desktop.bottom - windowHeight - 5;
        }

        // Move and size the suggestion window
        SetWindowPos(suggestionWindow, HWND_TOPMOST, posX, posY, windowWidth, windowHeight, SWP_SHOWWINDOW | SWP_NOACTIVATE);

        // Redraw the window
        InvalidateRect(suggestionWindow, NULL, TRUE);
    }
}

// Initialize system tray icon
void InitTrayIcon(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1; // Identifier of the icon
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;
    nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    wcscpy_s(nid.szTip, ARRAYSIZE(nid.szTip), L"T9 Application");

    Shell_NotifyIcon(NIM_ADD, &nid);

    // Create context menu
    hMenu = CreatePopupMenu();

    // Create submenu for Activate/Deactivate
    hSubMenu = CreatePopupMenu();
    AppendMenuW(hSubMenu, MF_STRING, 1004, L"Активировать");
    AppendMenuW(hSubMenu, MF_STRING, 1005, L"Деактивировать");
    AppendMenuW(hMenu, MF_STRING | MF_POPUP, (UINT_PTR)hSubMenu, L"Автодополнение");

    AppendMenuW(hMenu, MF_STRING, 1003, L"Клавиша дополнения");
    AppendMenuW(hMenu, MF_STRING, 1002, L"Выход");
}

// Show context menu on right-click
void ShowContextMenu(HWND hwnd) {
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(hwnd); // Necessary to ensure the menu closes correctly

    // Update submenu based on current state
    if (isAutocompleteEnabled) {
        EnableMenuItem(hSubMenu, 1004, MF_BYCOMMAND | MF_GRAYED);
        EnableMenuItem(hSubMenu, 1005, MF_BYCOMMAND | MF_ENABLED);
    } else {
        EnableMenuItem(hSubMenu, 1004, MF_BYCOMMAND | MF_ENABLED);
        EnableMenuItem(hSubMenu, 1005, MF_BYCOMMAND | MF_GRAYED);
    }

    UINT clicked = TrackPopupMenu(
        hMenu,
        TPM_RETURNCMD | TPM_NONOTIFY,
        pt.x,
        pt.y,
        0,
        hwnd,
        NULL
    );

    if (clicked == 1004) {
        // Activate the application
        isAutocompleteEnabled = true;
        MessageBoxW(NULL, L"Автодополнение активировано.", L"T9 Application", MB_OK);
    } else if (clicked == 1005) {
        // Deactivate the application
        isAutocompleteEnabled = false;
        MessageBoxW(NULL, L"Автодополнение деактивировано.", L"T9 Application", MB_OK);
        // Hide suggestion window
        suggestedWord.clear();
        ShowSuggestion();
    } else if (clicked == 1002) {
        // Exit the application
        Cleanup();
        PostQuitMessage(0);
    } else if (clicked == 1003) {
        // Open completion key dialog
        ShowCompletionKeyDialog(hwnd);
    }
}

// Function to show completion key dialog without resource files
void ShowCompletionKeyDialog(HWND parentHwnd) {
    isSettingCompletionKey = true; // Indicate that we're setting the completion key

    // Register window class
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = CompletionKeyWndProc;
    wc.hInstance = GetModuleHandle(NULL);
    wc.lpszClassName = L"CompletionKeyWindowClass";
    RegisterClassW(&wc);

    // Create window
    HWND hDlg = CreateWindowExW(
        WS_EX_DLGMODALFRAME,
        wc.lpszClassName,
        L"Настройка клавиши автодополнения",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 300, 150,
        parentHwnd,
        NULL,
        GetModuleHandle(NULL),
        NULL
    );

    ShowWindow(hDlg, SW_SHOW);
    UpdateWindow(hDlg);

    // Message loop for the dialog
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        if (!IsDialogMessage(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessage(&msg);
        }
        if (!IsWindow(hDlg)) {
            break;
        }
    }

    isSettingCompletionKey = false; // Reset the flag when done
}

// Window procedure for completion key dialog
LRESULT CALLBACK CompletionKeyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hStatic;
    static HWND hButtonChange;
    static HWND hButtonOK;

    switch (msg) {
    case WM_CREATE: {
        // Create static text
        hStatic = CreateWindowW(
            L"STATIC",
            (L"Текущая клавиша: " + GetKeyName(completionKey)).c_str(),
            WS_VISIBLE | WS_CHILD,
            20, 20, 260, 20,
            hwnd, NULL, NULL, NULL
        );

        // Create "Change" button
        hButtonChange = CreateWindowW(
            L"BUTTON",
            L"Изменить",
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            100, 50, 100, 30,
            hwnd, (HMENU)1, NULL, NULL
        );

        // Create "OK" button
        hButtonOK = CreateWindowW(
            L"BUTTON",
            L"OK",
            WS_VISIBLE | WS_CHILD | BS_DEFPUSHBUTTON,
            100, 90, 100, 30,
            hwnd, (HMENU)2, NULL, NULL
        );
        break;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == 1) { // "Change" button
            waitingForKey = true;
            SetWindowTextW(hStatic, L"Нажмите новую клавишу...");
            SetFocus(hwnd); // Set focus to capture keyboard input
        } else if (LOWORD(wParam) == 2) { // "OK" button
            DestroyWindow(hwnd);
        }
        break;
    case WM_KEYDOWN:
        if (waitingForKey) {
            completionKey = (UINT)wParam;
            std::wstring keyName = GetKeyName(completionKey);
            SetWindowTextW(hStatic, (L"Текущая клавиша: " + keyName).c_str());
            waitingForKey = false;
        }
        break;
    case WM_DESTROY:
        UnregisterClassW(L"CompletionKeyWindowClass", GetModuleHandle(NULL));
        break;
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// Cleanup resources
void Cleanup() {
    if (keyboardHook) {
        UnhookWindowsHookEx(keyboardHook);
        keyboardHook = NULL;
    }
    if (mouseHook) {
        UnhookWindowsHookEx(mouseHook);
        mouseHook = NULL;
    }
    if (hMenu) {
        DestroyMenu(hMenu);
        hMenu = NULL;
    }
    Shell_NotifyIcon(NIM_DELETE, &nid);
    if (suggestionWindow) {
        DestroyWindow(suggestionWindow);
        suggestionWindow = NULL;
    }
    // Save updated N-gram model and ignored words
    SaveNGramModel();
    SaveIgnoredWords();
}

// Function to get prediction based on N-gram model
std::wstring GetPrediction() {
    std::wstring prediction;

    // Use previous word and current input to find a suggestion
    if (!previousWord.empty()) {
        auto it = ngramModel.find(previousWord);
        if (it != ngramModel.end()) {
            int maxCount = 0;
            for (const auto& pair : it->second) {
                const std::wstring& nextWord = pair.first;
                int count = pair.second;

                if (ignoredWords.find(nextWord) != ignoredWords.end()) {
                    continue; // Skip ignored words
                }

                if (nextWord.length() >= currentInput.length() &&
                    nextWord.substr(0, currentInput.length()) == currentInput) {
                    if (count > maxCount) {
                        maxCount = count;
                        prediction = nextWord; // Return the full word
                    }
                }
            }
        }
    }

    // If no prediction based on previous word, check current input globally
    if (prediction.empty()) {
        int maxCount = 0;
        for (const auto& firstWordPair : ngramModel) {
            for (const auto& secondWordPair : firstWordPair.second) {
                const std::wstring& nextWord = secondWordPair.first;
                int count = secondWordPair.second;

                if (ignoredWords.find(nextWord) != ignoredWords.end()) {
                    continue; // Skip ignored words
                }

                if (nextWord.length() >= currentInput.length() &&
                    nextWord.substr(0, currentInput.length()) == currentInput) {
                    if (count > maxCount) {
                        maxCount = count;
                        prediction = nextWord; // Return the full word
                    }
                }
            }
        }
    }

    return prediction;
}

// Keyboard hook callback
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode != HC_ACTION || isSettingCompletionKey) {
        return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
    }

    KBDLLHOOKSTRUCT* pKeyStruct = (KBDLLHOOKSTRUCT*)lParam;

    // Skip processing of injected events
    if ((pKeyStruct->flags & LLKHF_INJECTED) != 0) {
        return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
    }

    if (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN) {
        // Handle modifier keys (Shift, Ctrl, Alt)
        if (pKeyStruct->vkCode == VK_SHIFT || pKeyStruct->vkCode == VK_LSHIFT || pKeyStruct->vkCode == VK_RSHIFT ||
            pKeyStruct->vkCode == VK_CONTROL || pKeyStruct->vkCode == VK_LCONTROL || pKeyStruct->vkCode == VK_RCONTROL ||
            pKeyStruct->vkCode == VK_MENU || pKeyStruct->vkCode == VK_LMENU || pKeyStruct->vkCode == VK_RMENU) {
            return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
        }

        // Check if the key is the completion key
        if (pKeyStruct->vkCode == completionKey) {
            if (isAutocompleteEnabled && !currentInput.empty() && !suggestedWord.empty()) {
                // Simulate backspaces to delete current input
                for (size_t i = 0; i < currentInput.length(); ++i) {
                    keybd_event(VK_BACK, 0, 0, 0);
                    keybd_event(VK_BACK, 0, KEYEVENTF_KEYUP, 0);
                }

                // Simulate typing the suggested word
                std::wstring textToInsert = suggestedWord;
                if (completionKey == VK_SPACE) {
                    textToInsert += L" ";
                }

                for (wchar_t ch : textToInsert) {
                    SHORT vk = VkKeyScanW(ch);
                    if (vk == -1) continue; // Cannot translate character

                    BYTE vkCode = LOBYTE(vk);
                    BYTE shiftState = HIBYTE(vk);

                    // Handle shift state
                    if (shiftState & 1) {
                        keybd_event(VK_SHIFT, 0, 0, 0);
                    }

                    keybd_event(vkCode, 0, 0, 0);
                    keybd_event(vkCode, 0, KEYEVENTF_KEYUP, 0);

                    if (shiftState & 1) {
                        keybd_event(VK_SHIFT, 0, KEYEVENTF_KEYUP, 0);
                    }
                }

                // Update N-gram model with user input
                if (!previousWord.empty()) {
                    ngramModel[previousWord][suggestedWord]++;
                }
                previousWord = suggestedWord;

                currentInput.clear();
                suggestedWord.clear();
                ShowSuggestion();

                return 1; // Suppress original key
            } else {
                // If no current input or suggestion, process normally
                if (!currentInput.empty()) {
                    // Update N-gram model with user input
                    if (!previousWord.empty()) {
                        ngramModel[previousWord][currentInput]++;
                    }
                    previousWord = currentInput;
                    currentInput.clear();
                } else {
                    previousWord.clear();
                }
                suggestedWord.clear();
                ShowSuggestion();
                return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
            }
        }

        // Check if the key is Backspace
        if (pKeyStruct->vkCode == VK_BACK) {
            if (!suggestedWord.empty()) {
                // Hide the suggestion and suppress the Backspace key
                suggestedWord.clear();
                ShowSuggestion();
                return 1; // Suppress the backspace key
            } else {
                // Allow Backspace to proceed
                return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
            }
        }

        // Check if the key is a navigation key (arrow keys)
        if (pKeyStruct->vkCode == VK_LEFT || pKeyStruct->vkCode == VK_RIGHT ||
            pKeyStruct->vkCode == VK_UP || pKeyStruct->vkCode == VK_DOWN) {
            // User moved the caret
            if (isAutocompleteEnabled) {
                currentInput = GetWordUnderCaret();
                if (!currentInput.empty()) {
                    suggestedWord = GetPrediction();
                    ShowSuggestion();
                } else {
                    currentInput.clear();
                    suggestedWord.clear();
                    ShowSuggestion();
                }
            }
            return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
        }

        // Get keyboard state
        BYTE keyboardState[256];
        if (!GetKeyboardState(keyboardState)) {
            return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
        }

        // Buffer for translated characters
        wchar_t buffer[5];
        int result = ToUnicodeEx(pKeyStruct->vkCode, pKeyStruct->scanCode, keyboardState, buffer, 4, 0, GetKeyboardLayout(0));

        if (result > 0) {
            buffer[result] = L'\0';
            wchar_t ch = buffer[0];

            // Check if character is a letter
            if (IsRussianLetter(ch)) {
                ch = ToLowerRussian(ch);
                currentInput += ch;

                // Get prediction
                if (isAutocompleteEnabled) {
                    suggestedWord = GetPrediction();
                    ShowSuggestion();
                }
            } else {
                // If non-letter character, reset input
                if (!currentInput.empty()) {
                    // Update N-gram model with user input
                    if (!previousWord.empty()) {
                        ngramModel[previousWord][currentInput]++;
                    }
                    previousWord = currentInput;
                } else {
                    previousWord.clear();
                }
                currentInput.clear();
                suggestedWord.clear();
                ShowSuggestion();
            }
        } else {
            // Other keys
            if (isAutocompleteEnabled) {
                currentInput = GetWordUnderCaret();
                if (!currentInput.empty()) {
                    suggestedWord = GetPrediction();
                    ShowSuggestion();
                } else {
                    currentInput.clear();
                    suggestedWord.clear();
                    ShowSuggestion();
                }
            } else {
                currentInput.clear();
                suggestedWord.clear();
                ShowSuggestion();
            }
        }
    }

    return CallNextHookEx(keyboardHook, nCode, wParam, lParam);
}

// Mouse hook callback
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION) {
        PMSLLHOOKSTRUCT pMouseStruct = (PMSLLHOOKSTRUCT)lParam;
        if (pMouseStruct != NULL) {
            if (wParam == WM_LBUTTONDOWN || wParam == WM_RBUTTONDOWN) {
                HWND hWndUnderCursor = WindowFromPoint(pMouseStruct->pt);
                if (hWndUnderCursor != suggestionWindow && hWndUnderCursor != GetParent(suggestionWindow)) {
                    // Hide suggestion on mouse click outside the suggestion window
                    suggestedWord.clear();
                    ShowSuggestion();
                }
            }
            if (wParam == WM_LBUTTONDOWN) {
                // Update currentInput based on word under caret
                if (isAutocompleteEnabled) {
                    currentInput = GetWordUnderCaret();
                    if (!currentInput.empty()) {
                        suggestedWord = GetPrediction();
                        ShowSuggestion();
                    } else {
                        currentInput.clear();
                        suggestedWord.clear();
                        ShowSuggestion();
                    }
                }
            }
        }
    }
    return CallNextHookEx(mouseHook, nCode, wParam, lParam);
}

// Main function
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Load N-gram model from corpus.txt
    LoadNGramModel();

    // Load user history
    LoadUserHistory();

    // Load ignored words
    LoadIgnoredWords();

    // Create suggestion window
    CreateSuggestionWindow(hInstance);

    // Create a hidden window to receive messages
    WNDCLASSW wc = {0};
    wc.lpfnWndProc = SuggestionWndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = L"MainWindowClass";

    RegisterClassW(&wc);

    HWND hwndMain = CreateWindowW(
        wc.lpszClassName,
        L"T9 Application",
        WS_OVERLAPPEDWINDOW,
        0, 0, 0, 0,
        NULL, NULL, hInstance, NULL
    );

    // Initialize tray icon
    InitTrayIcon(hwndMain);

    // Set keyboard hook
    keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, hInstance, 0);
    if (!keyboardHook) {
        MessageBoxW(NULL, L"Не удалось установить хук клавиатуры!", L"Ошибка", MB_ICONERROR);
        return 1;
    }

    // Set mouse hook
    mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, hInstance, 0);
    if (!mouseHook) {
        MessageBoxW(NULL, L"Не удалось установить хук мыши!", L"Ошибка", MB_ICONERROR);
        UnhookWindowsHookEx(keyboardHook);
        return 1;
    }

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        if (msg.message == WM_TRAYICON) {
            SuggestionWndProc(hwndMain, msg.message, msg.wParam, msg.lParam);
        } else {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    // Cleanup
    Cleanup();

    return 0;
}
