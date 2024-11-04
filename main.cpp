// Version 1.5
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
#include <shlobj.h> // Для SHGetFolderPathW

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

// N-gram model variables (Unigrams, Bigrams, and Trigrams)
std::unordered_map<std::wstring, int> unigramModel;
std::unordered_map<std::wstring, std::unordered_map<std::wstring, int>> bigramModel;
std::unordered_map<std::wstring, std::unordered_map<std::wstring, int>> trigramModel;
std::wstring previousWord = L"";

// Variable for completion key
UINT completionKey = VK_SPACE; // Default to space key

// Flags
bool isSettingCompletionKey = false;
bool waitingForKey = false;
bool isAutocompleteEnabled = true;

// Ignored words set
std::unordered_set<std::wstring> ignoredWords; // Set for ignored words

// Flag to indicate if context menu is open
bool isContextMenuOpen = false;

// Function declarations
void LoadNGramModels();
void SaveNGramModels();
void LoadIgnoredWords();
void SaveIgnoredWords();
void BuildNGramModelsFromCorpus(const std::wstring& corpusPath);
void CreateSuggestionWindow(HINSTANCE hInstance);
void ShowSuggestion();
LRESULT CALLBACK SuggestionWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
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
std::wstring GetPrediction();
void UpdateNGramModelsOnSelection(const std::wstring& selectedWord);

// Function to check if character is a Russian letter
bool IsRussianLetter(wchar_t ch) {
    return (ch >= L'А' && ch <= L'я') || ch == L'Ё' || ch == L'ё';
}

// Function to convert Russian character to lowercase
wchar_t ToLowerRussian(wchar_t ch) {
    if (ch >= L'А' && ch <= L'Я') {
        return ch + 32; // Разница между заглавными и строчными буквами в Unicode Cyrillic
    }
    else if (ch == L'Ё') {
        return L'ё';
    }
    else {
        return ch;
    }
}

// Function to get key name from virtual key code
std::wstring GetKeyName(UINT vkCode) {
    UINT scanCode = MapVirtualKey(vkCode, MAPVK_VK_TO_VSC);
    WCHAR keyName[128];
    if (GetKeyNameTextW(scanCode << 16, keyName, 128) > 0) {
        return keyName;
    }
    else {
        return L"Unknown";
    }
}

// Function to load N-gram models (Unigrams, Bigrams, Trigrams) from corpus.txt
void LoadNGramModels() {
    // Построение N-грамм из corpus.txt
    BuildNGramModelsFromCorpus(L"corpus.txt");

    // Если вы хотите дополнительно загружать существующие модели из файлов, реализуйте это здесь.
    // Например, для обновления моделей на основе пользовательского ввода.
}

// Function to build N-gram models from corpus.txt
void BuildNGramModelsFromCorpus(const std::wstring& corpusPath) {
    OutputDebugStringW(L"Функция BuildNGramModelsFromCorpus вызвана.\n");

    std::wifstream corpusFile(corpusPath.c_str(), std::ios::binary); // Исправлено здесь
    if (!corpusFile.is_open()) {
        MessageBoxW(NULL, L"Не удалось открыть файл corpus.txt!", L"Ошибка", MB_ICONERROR);
        OutputDebugStringW(L"Не удалось открыть файл corpus.txt.\n");
        return;
    }

    // Установите правильную локаль для чтения UTF-8 (или измените на нужную кодировку)
    corpusFile.imbue(std::locale(corpusFile.getloc(),
        new std::codecvt_utf8<wchar_t>)); // Изменено на UTF-8

    std::wstring line;
    std::vector<std::wstring> words;
    int lineCount = 0;
    int wordCount = 0;

    while (std::getline(corpusFile, line)) {
        lineCount++;
        std::wstringstream ss(line);
        std::wstring word;
        while (ss >> word) {
            // Очистка слова от пунктуации и преобразование в нижний регистр
            std::wstring cleanWord;
            for (auto& ch : word) {
                if (IsRussianLetter(ch)) {
                    cleanWord += ToLowerRussian(ch);
                }
            }
            if (!cleanWord.empty()) {
                words.push_back(cleanWord);
                unigramModel[cleanWord]++;
                wordCount++;
            }
        }
    }

    corpusFile.close();

    // Построение биграмм и триграмм
    for (size_t i = 0; i < words.size(); ++i) {
        if (i < words.size() - 1) {
            bigramModel[words[i]][words[i + 1]]++;
        }
        if (i < words.size() - 2) {
            std::wstring key = words[i] + L" " + words[i + 1];
            trigramModel[key][words[i + 2]]++;
        }
    }

    // Сохранение моделей
    SaveNGramModels();

    // Отладочные сообщения
    std::wstring debugMsg = L"Количество обработанных строк: " + std::to_wstring(lineCount) + L"\n";
    OutputDebugStringW(debugMsg.c_str());

    debugMsg = L"Количество обработанных слов: " + std::to_wstring(wordCount) + L"\n";
    OutputDebugStringW(debugMsg.c_str());

    OutputDebugStringW(L"N-граммы успешно построены из corpus.txt.\n");
}

// Function to save N-gram models (Unigrams, Bigrams, Trigrams)
void SaveNGramModels() {
    // Save Unigrams to unigrams.txt
    std::wofstream unigramFile(L"unigrams.txt", std::ios::trunc | std::ios::binary);
    if (!unigramFile.is_open()) {
        MessageBoxW(NULL, L"Не удалось открыть файл unigrams.txt для записи!", L"Ошибка", MB_ICONERROR);
        OutputDebugStringW(L"Не удалось открыть файл unigrams.txt для записи.\n");
        return;
    }

    // Set UTF-8 encoding
    unigramFile.imbue(std::locale(unigramFile.getloc(),
        new std::codecvt_utf8<wchar_t>)); // Изменено на UTF-8

    for (const auto& pair : unigramModel) {
        unigramFile << pair.first << L" " << pair.second << L"\n";
    }

    unigramFile.close();

    // Save Bigrams to bigrams.txt
    std::wofstream bigramFile(L"bigrams.txt", std::ios::trunc | std::ios::binary);
    if (!bigramFile.is_open()) {
        MessageBoxW(NULL, L"Не удалось открыть файл bigrams.txt для записи!", L"Ошибка", MB_ICONERROR);
        OutputDebugStringW(L"Не удалось открыть файл bigrams.txt для записи.\n");
        return;
    }

    // Set UTF-8 encoding
    bigramFile.imbue(std::locale(bigramFile.getloc(),
        new std::codecvt_utf8<wchar_t>)); // Изменено на UTF-8

    for (const auto& firstWordPair : bigramModel) {
        const std::wstring& firstWord = firstWordPair.first;
        for (const auto& secondWordPair : firstWordPair.second) {
            const std::wstring& secondWord = secondWordPair.first;
            int count = secondWordPair.second;
            bigramFile << firstWord << L" " << secondWord << L" " << count << L"\n";
        }
    }

    bigramFile.close();

    // Save Trigrams to trigrams.txt
    std::wofstream trigramFile(L"trigrams.txt", std::ios::trunc | std::ios::binary);
    if (!trigramFile.is_open()) {
        MessageBoxW(NULL, L"Не удалось открыть файл trigrams.txt для записи!", L"Ошибка", MB_ICONERROR);
        OutputDebugStringW(L"Не удалось открыть файл trigrams.txt для записи.\n");
        return;
    }

    // Set UTF-8 encoding
    trigramFile.imbue(std::locale(trigramFile.getloc(),
        new std::codecvt_utf8<wchar_t>)); // Изменено на UTF-8

    for (const auto& firstWordPair : trigramModel) {
        const std::wstring& key = firstWordPair.first; // key is "word1 word2"
        std::wstring word1, word2;
        size_t spacePos = key.find_first_of(L' ');
        if (spacePos != std::wstring::npos) {
            word1 = key.substr(0, spacePos);
            word2 = key.substr(spacePos + 1);
        }
        else {
            // Если ключ не содержит пробела, пропускаем запись
            continue;
        }

        for (const auto& thirdWordPair : firstWordPair.second) {
            const std::wstring& thirdWord = thirdWordPair.first;
            int count = thirdWordPair.second;
            trigramFile << word1 << L" " << word2 << L" " << thirdWord << L" " << count << L"\n";
        }
    }

    trigramFile.close();

    OutputDebugStringW(L"N-граммы успешно сохранены в файлы.\n");
}

// Function to load ignored words from ignored_words.txt
void LoadIgnoredWords() {
    std::wifstream ignoredFile(L"ignored_words.txt", std::ios::binary);
    if (!ignoredFile.is_open()) {
        // Create the file if it doesn't exist
        std::wofstream newIgnoredFile(L"ignored_words.txt", std::ios::trunc | std::ios::binary);
        if (!newIgnoredFile.is_open()) {
            MessageBoxW(NULL, L"Не удалось создать файл ignored_words.txt!", L"Ошибка", MB_ICONERROR);
            OutputDebugStringW(L"Не удалось создать файл ignored_words.txt.\n");
            return;
        }
        // Set UTF-8 encoding
        newIgnoredFile.imbue(std::locale(newIgnoredFile.getloc(),
            new std::codecvt_utf8<wchar_t>)); // Изменено на UTF-8
        newIgnoredFile.close();
        return;
    }

    // Set UTF-8 encoding
    ignoredFile.imbue(std::locale(ignoredFile.getloc(),
        new std::codecvt_utf8<wchar_t>)); // Изменено на UTF-8

    std::wstring word;
    while (std::getline(ignoredFile, word)) {
        if (!word.empty()) {
            ignoredWords.insert(word);
        }
    }

    ignoredFile.close();

    OutputDebugStringW(L"Ignored words успешно загружены.\n");
}

// Function to save ignored words to ignored_words.txt
void SaveIgnoredWords() {
    std::wofstream ignoredFile(L"ignored_words.txt", std::ios::trunc | std::ios::binary);
    if (!ignoredFile.is_open()) {
        MessageBoxW(NULL, L"Не удалось открыть файл ignored_words.txt для записи!", L"Ошибка", MB_ICONERROR);
        OutputDebugStringW(L"Не удалось открыть файл ignored_words.txt для записи.\n");
        return;
    }

    // Set UTF-8 encoding
    ignoredFile.imbue(std::locale(ignoredFile.getloc(),
        new std::codecvt_utf8<wchar_t>)); // Изменено на UTF-8

    for (const auto& word : ignoredWords) {
        ignoredFile << word << L"\n";
    }

    ignoredFile.close();

    OutputDebugStringW(L"Ignored words успешно сохранены.\n");
}

// Function to remove a word from the dictionary
void RemoveWordFromDictionary(const std::wstring& word) {
    // Преобразование слова в нижний регистр
    std::wstring lowerWord = word;
    for (auto& ch : lowerWord) {
        ch = ToLowerRussian(ch);
    }

    // Удаление слова из unigramModel
    unigramModel.erase(lowerWord);

    // Удаление слова из всех записей биграмм модели
    for (auto& firstWordPair : bigramModel) {
        firstWordPair.second.erase(lowerWord);
    }
    // Также удаление записей, где это слово является первым
    bigramModel.erase(lowerWord);

    // Удаление слова из всех записей триграмм модели
    for (auto& firstWordPair : trigramModel) {
        firstWordPair.second.erase(lowerWord);
    }
    // Также удаление записей, где это слово является первым
    trigramModel.erase(lowerWord);

    // Добавление слова в игнорируемые
    ignoredWords.insert(lowerWord);

    // Сохранение обновленных данных
    SaveNGramModels();
    SaveIgnoredWords();

    // Отладочное сообщение
    std::wstring debugMsg = L"Слово удалено и добавлено в игнорируемые: " + lowerWord + L"\n";
    OutputDebugStringW(debugMsg.c_str());
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

    // Проверка, является ли окно элементом редактирования
    const int classNameSize = 256;
    WCHAR className[classNameSize];
    GetClassNameW(hFocus, className, classNameSize);
    if (wcscmp(className, L"Edit") != 0) {
        return L"";
    }

    LONG_PTR style = GetWindowLongPtrW(hFocus, GWL_STYLE);
    if (style & ES_READONLY) { // Изменено здесь
        return L"";
    }

    // Get the text from the control
    int length = GetWindowTextLengthW(hFocus);
    if (length == 0) return L"";

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

    // Отладочное сообщение
    std::wstring debugMsg = L"Получено слово под кареткой: " + wordUnderCaret + L"\n";
    OutputDebugStringW(debugMsg.c_str());

    return wordUnderCaret;
}

// Function to get prediction based on N-gram models (Trigrams, Bigrams, and Unigrams) with weighting
std::wstring GetPrediction() {
    std::wstring prediction;
    double maxScore = 0.0;

    // Определение весов для моделей
    const double unigramWeight = 1.0;
    const double bigramWeight = 2.0;
    const double trigramWeight = 3.0;

    // Разделяем предыдущие слова
    std::wstring key = previousWord;
    size_t firstSpace = key.find_first_of(L' ');
    size_t secondSpace = key.find_first_of(L' ', firstSpace + 1);
    if (firstSpace != std::wstring::npos && secondSpace != std::wstring::npos) {
        key = key.substr(secondSpace + 1);
    }
    else {
        // Если нет двух предыдущих слов, используем одно
        if (firstSpace != std::wstring::npos) {
            key = key.substr(firstSpace + 1);
        }
    }

    // Сбор кандидатов из триграмм
    if (key.find_first_of(L' ') != std::wstring::npos) {
        auto it = trigramModel.find(key);
        if (it != trigramModel.end()) {
            for (const auto& pair : it->second) {
                const std::wstring& nextWord = pair.first;
                int count = pair.second;

                if (ignoredWords.find(nextWord) != ignoredWords.end()) {
                    continue; // Пропускаем игнорируемые слова
                }

                if (nextWord.length() >= currentInput.length() &&
                    nextWord.substr(0, currentInput.length()) == currentInput) {
                    double score = trigramWeight * count;
                    if (score > maxScore) {
                        maxScore = score;
                        prediction = nextWord;
                    }
                }
            }
        }
    }

    // Сбор кандидатов из биграмм
    if (!key.empty() && key.find_first_of(L' ') == std::wstring::npos) {
        auto it = bigramModel.find(key);
        if (it != bigramModel.end()) {
            for (const auto& pair : it->second) {
                const std::wstring& nextWord = pair.first;
                int count = pair.second;

                if (ignoredWords.find(nextWord) != ignoredWords.end()) {
                    continue; // Пропускаем игнорируемые слова
                }

                if (nextWord.length() >= currentInput.length() &&
                    nextWord.substr(0, currentInput.length()) == currentInput) {
                    double score = bigramWeight * count;
                    if (score > maxScore) {
                        maxScore = score;
                        prediction = nextWord;
                    }
                }
            }
        }
    }

    // Сбор кандидатов из униграмм
    if (!currentInput.empty()) {
        for (const auto& pair : unigramModel) {
            const std::wstring& word = pair.first;
            int count = pair.second;

            if (ignoredWords.find(word) != ignoredWords.end()) {
                continue; // Пропускаем игнорируемые слова
            }

            if (word.length() >= currentInput.length() &&
                word.substr(0, currentInput.length()) == currentInput) {
                double score = unigramWeight * count;
                if (score > maxScore) {
                    maxScore = score;
                    prediction = word;
                }
            }
        }
    }

    // Отладочное сообщение
    std::wstring debugMsg = L"Предсказание: " + prediction + L"\n";
    OutputDebugStringW(debugMsg.c_str());

    return prediction;
}

// Function to update N-gram models upon selecting a prediction
void UpdateNGramModelsOnSelection(const std::wstring& selectedWord) {
    if (!previousWord.empty()) {
        size_t firstSpace = previousWord.find_first_of(L' ');
        size_t secondSpace = previousWord.find_first_of(L' ', firstSpace + 1);
        std::wstring newKey;
        if (firstSpace != std::wstring::npos && secondSpace != std::wstring::npos) {
            newKey = previousWord.substr(secondSpace + 1) + L" " + selectedWord;
            trigramModel[previousWord][selectedWord]++;
        }
        else if (firstSpace != std::wstring::npos) {
            newKey = previousWord.substr(firstSpace + 1) + L" " + selectedWord;
            trigramModel[previousWord][selectedWord]++;
        }
        else {
            newKey = selectedWord;
            bigramModel[previousWord][selectedWord]++;
        }
        previousWord = newKey;
    }
    else {
        // Если previousWord пусто, устанавливаем текущий ввод как предыдущий
        previousWord = selectedWord;
    }

    // Отладочное сообщение
    std::wstring debugMsg = L"N-грамма обновлена с выбранным словом: " + selectedWord + L"\n";
    OutputDebugStringW(debugMsg.c_str());
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
    case WM_RBUTTONDOWN: {
        // Показ контекстного меню для удаления или игнорирования слова
        POINT pt;
        GetCursorPos(&pt);
        HMENU hContextMenu = CreatePopupMenu();
        AppendMenuW(hContextMenu, MF_STRING, 2001, L"Удалить");
        AppendMenuW(hContextMenu, MF_STRING, 2002, L"Игнорировать");

        SetForegroundWindow(hwnd); // Необходимо для корректного закрытия меню

        // Установка флага перед открытием меню
        isContextMenuOpen = true;

        UINT clicked = TrackPopupMenu(
            hContextMenu,
            TPM_RETURNCMD | TPM_RIGHTBUTTON,
            pt.x,
            pt.y,
            0,
            hwnd,
            NULL
        );

        // Сброс флага после закрытия меню
        isContextMenuOpen = false;

        DestroyMenu(hContextMenu);

        // Получение предложенного слова из текста окна подсказки
        wchar_t buffer[256];
        GetWindowTextW(hwnd, buffer, 256);
        std::wstring word(buffer);

        if (!word.empty()) { // Убедимся, что слово не пустое
            if (clicked == 2001) {
                // Удаление слова из словаря
                RemoveWordFromDictionary(word);
                // Очистка предложения и скрытие окна
                suggestedWord.clear();
                ShowSuggestion();

                // Отладочное сообщение
                std::wstring debugMsg = L"Слово удалено: " + word + L"\n";
                OutputDebugStringW(debugMsg.c_str());
            }
            else if (clicked == 2002) {
                // Добавление слова в игнорируемые
                std::wstring lowerWord = word;
                for (auto& ch : lowerWord) {
                    ch = ToLowerRussian(ch);
                }
                ignoredWords.insert(lowerWord);
                SaveIgnoredWords();
                // Очистка предложения и скрытие окна
                suggestedWord.clear();
                ShowSuggestion();

                // Отладочное сообщение
                std::wstring debugMsg = L"Слово добавлено в игнорируемые: " + lowerWord + L"\n";
                OutputDebugStringW(debugMsg.c_str());
            }
        }

        break;
    }
    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
    return 0;
}

// Оконная процедура для основного окна
LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_TRAYICON:
        if (lParam == WM_RBUTTONUP) {
            ShowContextMenu(hwnd);
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        break;
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

    OutputDebugStringW(L"Suggestion window создано.\n"); // Отладочное сообщение
}

// Function to show suggestion near the caret as a tooltip
void ShowSuggestion() {
    if (suggestedWord.empty()) {
        ShowWindow(suggestionWindow, SW_HIDE);
        OutputDebugStringW(L"Предложение скрыто.\n"); // Отладочное сообщение
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

        // Отладочное сообщение
        std::wstring debugMsg = L"Предложение показано: " + suggestedWord + L"\n";
        OutputDebugStringW(debugMsg.c_str());
    }
    else {
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

        // Отладочное сообщение
        std::wstring debugMsg = L"Предложение показано на позиции курсора: " + suggestedWord + L"\n";
        OutputDebugStringW(debugMsg.c_str());
    }
}

// Initialize system tray icon with custom icon
void InitTrayIcon(HWND hwnd) {
    nid.cbSize = sizeof(NOTIFYICONDATA);
    nid.hWnd = hwnd;
    nid.uID = 1; // Identifier of the icon
    nid.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    nid.uCallbackMessage = WM_TRAYICON;

    // Загрузка пользовательской иконки из файла
    HICON hIcon = (HICON)LoadImageW(
        NULL,
        L"icon.ico", // Убедитесь, что файл icon.ico существует в рабочей директории
        IMAGE_ICON,
        32, 32, // Размер иконки
        LR_LOADFROMFILE
    );

    if (hIcon) {
        nid.hIcon = hIcon;
    }
    else {
        // Если не удалось загрузить пользовательскую иконку, используем стандартную
        nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    }

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

    // Отладочное сообщение
    OutputDebugStringW(L"System tray icon и меню инициализированы.\n");
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
    }
    else {
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

        // Отладочное сообщение
        OutputDebugStringW(L"Автодополнение активировано.\n");
    }
    else if (clicked == 1005) {
        // Deactivate the application
        isAutocompleteEnabled = false;
        MessageBoxW(NULL, L"Автодополнение деактивировано.", L"T9 Application", MB_OK);
        // Hide suggestion window
        suggestedWord.clear();
        ShowSuggestion();

        // Отладочное сообщение
        OutputDebugStringW(L"Автодополнение деактивировано.\n");
    }
    else if (clicked == 1002) {
        // Exit the application
        Cleanup();
        PostQuitMessage(0);

        // Отладочное сообщение
        OutputDebugStringW(L"Приложение завершает работу.\n");
    }
    else if (clicked == 1003) {
        // Open completion key dialog
        ShowCompletionKeyDialog(hwnd);

        // Отладочное сообщение
        OutputDebugStringW(L"Открыт диалог настройки клавиши дополнения.\n");
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

    // Отладочное сообщение
    OutputDebugStringW(L"Диалог настройки клавиши дополнения открыт.\n");

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

    // Отладочное сообщение
    OutputDebugStringW(L"Диалог настройки клавиши дополнения закрыт.\n");
}

// Window procedure for completion key dialog
LRESULT CALLBACK CompletionKeyWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    static HWND hStatic;
    static HWND hButtonChange;
    static HWND hButtonOK;

    switch (msg) {
    case WM_CREATE: {
        // Create static text
        std::wstring initialText = L"Текущая клавиша: " + GetKeyName(completionKey);
        hStatic = CreateWindowW(
            L"STATIC",
            initialText.c_str(),
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

            // Отладочное сообщение
            OutputDebugStringW(L"Ожидание новой клавиши дополнения...\n");
        }
        else if (LOWORD(wParam) == 2) { // "OK" button
            DestroyWindow(hwnd);

            // Отладочное сообщение
            OutputDebugStringW(L"Нажата кнопка OK в диалоге настройки клавиши.\n");
        }
        break;
    case WM_KEYDOWN:
        if (waitingForKey) {
            completionKey = (UINT)wParam;
            std::wstring keyName = GetKeyName(completionKey);
            std::wstring newText = L"Текущая клавиша: " + keyName;
            SetWindowTextW(hStatic, newText.c_str());
            waitingForKey = false;

            // Отладочное сообщение
            std::wstring debugMsg = L"Клавиша дополнения изменена на: " + keyName + L"\n";
            OutputDebugStringW(debugMsg.c_str());
        }
        break;
    case WM_DESTROY:
        UnregisterClassW(L"CompletionKeyWindowClass", GetModuleHandle(NULL));

        // Отладочное сообщение
        OutputDebugStringW(L"Диалог настройки клавиши дополнения уничтожен.\n");
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
    // Освобождение иконки, если она загружена
    if (nid.hIcon) {
        DestroyIcon(nid.hIcon);
    }
    // Save updated unigramModel, bigramModel, and trigramModel
    SaveNGramModels();
    // Save ignored words
    SaveIgnoredWords();

    // Отладочное сообщение
    OutputDebugStringW(L"Ресурсы очищены и сохранены.\n");
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

                // Update N-gram models with user input
                UpdateNGramModelsOnSelection(suggestedWord);

                currentInput.clear();
                suggestedWord.clear();
                ShowSuggestion();

                // Отладочное сообщение
                OutputDebugStringW(L"Предложение принято и вставлено.\n");

                return 1; // Suppress original key
            }
            else {
                // If no current input or suggestion, process normally
                if (!currentInput.empty()) {
                    // Update N-gram models with user input
                    if (!previousWord.empty()) {
                        // Добавляем триграмму или биграмму в зависимости от количества слов
                        size_t firstSpace = previousWord.find_first_of(L' ');
                        size_t secondSpace = previousWord.find_first_of(L' ', firstSpace + 1);
                        std::wstring newKey;
                        if (firstSpace != std::wstring::npos && secondSpace != std::wstring::npos) {
                            newKey = previousWord.substr(secondSpace + 1) + L" " + currentInput;
                            trigramModel[previousWord][currentInput]++;
                        }
                        else if (firstSpace != std::wstring::npos) {
                            newKey = previousWord.substr(firstSpace + 1) + L" " + currentInput;
                            trigramModel[previousWord][currentInput]++;
                        }
                        else {
                            newKey = currentInput;
                            bigramModel[previousWord][currentInput]++;
                        }
                        previousWord = newKey;
                    }
                    else {
                        // Если previousWord пусто, устанавливаем текущий ввод как предыдущий
                        previousWord = currentInput;
                    }
                    currentInput.clear();
                }
                else {
                    // Если currentInput пусто, сбрасываем previousWord
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

                // Отладочное сообщение
                OutputDebugStringW(L"Backspace нажата, предложение скрыто.\n");

                return 1; // Suppress the backspace key
            }
            else {
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

                    // Отладочное сообщение
                    OutputDebugStringW(L"Caret перемещен, предложение обновлено.\n");
                }
                else {
                    currentInput.clear();
                    suggestedWord.clear();
                    ShowSuggestion();

                    // Отладочное сообщение
                    OutputDebugStringW(L"Caret перемещен, предложение скрыто.\n");
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

                    // Отладочное сообщение
                    std::wstring debugMsg = L"Добавлена буква: " + std::wstring(1, ch) + L", предложение: " + suggestedWord + L"\n";
                    OutputDebugStringW(debugMsg.c_str());
                }
            }
            else if (ch == L' ') {
                // Обработка ввода пробела
                if (!currentInput.empty()) {
                    // Обновляем предыдущие слова, добавляя текущий ввод
                    if (!previousWord.empty()) {
                        size_t firstSpace = previousWord.find_first_of(L' ');
                        size_t secondSpace = previousWord.find_first_of(L' ', firstSpace + 1);
                        std::wstring newKey;
                        if (firstSpace != std::wstring::npos && secondSpace != std::wstring::npos) {
                            newKey = previousWord.substr(secondSpace + 1) + L" " + currentInput;
                            trigramModel[previousWord][currentInput]++;
                        }
                        else if (firstSpace != std::wstring::npos) {
                            newKey = previousWord.substr(firstSpace + 1) + L" " + currentInput;
                            trigramModel[previousWord][currentInput]++;
                        }
                        else {
                            newKey = currentInput;
                            bigramModel[previousWord][currentInput]++;
                        }

                        previousWord = newKey;
                    }
                    else {
                        previousWord = currentInput;
                    }

                    currentInput.clear();
                    suggestedWord = GetPrediction();
                    ShowSuggestion();

                    // Отладочное сообщение
                    std::wstring debugMsg = L"Введен пробел, предложение: " + suggestedWord + L"\n";
                    OutputDebugStringW(debugMsg.c_str());
                }
            }
            else {
                // If non-letter character, reset input
                if (!currentInput.empty()) {
                    // Update N-gram models with user input
                    if (!previousWord.empty()) {
                        size_t firstSpace = previousWord.find_first_of(L' ');
                        size_t secondSpace = previousWord.find_first_of(L' ', firstSpace + 1);
                        std::wstring newKey;
                        if (firstSpace != std::wstring::npos && secondSpace != std::wstring::npos) {
                            newKey = previousWord.substr(secondSpace + 1) + L" " + currentInput;
                            trigramModel[previousWord][currentInput]++;
                        }
                        else if (firstSpace != std::wstring::npos) {
                            newKey = previousWord.substr(firstSpace + 1) + L" " + currentInput;
                            trigramModel[previousWord][currentInput]++;
                        }
                        else {
                            newKey = currentInput;
                            bigramModel[previousWord][currentInput]++;
                        }
                        previousWord = newKey;
                    }
                    else {
                        previousWord = currentInput;
                    }
                    currentInput.clear();
                }
                else {
                    previousWord.clear();
                }
                suggestedWord.clear();
                ShowSuggestion();

                // Отладочное сообщение
                OutputDebugStringW(L"Введен не буква или пробел, предложение скрыто.\n");
            }
        }
        else {
            // Other keys
            if (isAutocompleteEnabled) {
                currentInput = GetWordUnderCaret();
                if (!currentInput.empty()) {
                    suggestedWord = GetPrediction();
                    ShowSuggestion();

                    // Отладочное сообщение
                    OutputDebugStringW(L"Введен символ без преобразования, предложение обновлено.\n");
                }
                else {
                    currentInput.clear();
                    suggestedWord.clear();
                    ShowSuggestion();

                    // Отладочное сообщение
                    OutputDebugStringW(L"Введен символ без преобразования, предложение скрыто.\n");
                }
            }
            else {
                currentInput.clear();
                suggestedWord.clear();
                ShowSuggestion();

                // Отладочное сообщение
                OutputDebugStringW(L"Автодополнение отключено, предложение скрыто.\n");
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
                    // Проверка флага: если контекстное меню открыто, не скрывать окно
                    if (!isContextMenuOpen) {
                        // Hide suggestion on mouse click outside the suggestion window
                        suggestedWord.clear();
                        ShowSuggestion();

                        // Отладочное сообщение
                        OutputDebugStringW(L"Мышь кликнута вне окна подсказки, предложение скрыто.\n");
                    }
                }
            }
            if (wParam == WM_LBUTTONDOWN) {
                // Update currentInput based on word under caret
                if (isAutocompleteEnabled) {
                    currentInput = GetWordUnderCaret();
                    if (!currentInput.empty()) {
                        suggestedWord = GetPrediction();
                        ShowSuggestion();

                        // Отладочное сообщение
                        OutputDebugStringW(L"Левый клик, предложение обновлено.\n");
                    }
                    else {
                        currentInput.clear();
                        suggestedWord.clear();
                        ShowSuggestion();

                        // Отладочное сообщение
                        OutputDebugStringW(L"Левый клик, предложение скрыто.\n");
                    }
                }
            }
        }
    }
    return CallNextHookEx(mouseHook, nCode, wParam, lParam);
}

// Main function
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int) {
    // Load N-gram models (Unigrams, Bigrams, Trigrams)
    LoadNGramModels();

    // Load ignored words
    LoadIgnoredWords();

    // Create suggestion window
    CreateSuggestionWindow(hInstance);

    // Register class for main window
    WNDCLASSW mainWc = {0};
    mainWc.lpfnWndProc = MainWndProc; // Используем отдельную оконную процедуру
    mainWc.hInstance = hInstance;
    mainWc.lpszClassName = L"MainWindowClass";

    RegisterClassW(&mainWc);

    // Create main window
    HWND hwndMain = CreateWindowW(
        mainWc.lpszClassName,
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
        OutputDebugStringW(L"Не удалось установить хук клавиатуры.\n");
        return 1;
    }

    // Set mouse hook
    mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseProc, hInstance, 0);
    if (!mouseHook) {
        MessageBoxW(NULL, L"Не удалось установить хук мыши!", L"Ошибка", MB_ICONERROR);
        OutputDebugStringW(L"Не удалось установить хук мыши.\n");
        UnhookWindowsHookEx(keyboardHook);
        return 1;
    }

    // Message loop
    MSG msg;
    while (GetMessageW(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    // Cleanup
    Cleanup();

    return 0;
}
