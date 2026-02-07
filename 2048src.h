#pragma once
#ifndef GAME2048_H
#define GAME2048_H

#include <iostream>
#include <vector>
#include <cstdlib>
#include <ctime>
#include <cctype>
#include <sstream>
#include <iomanip>
#include <string>
#include <map>
#include <algorithm>
#include <fstream>
#include <cstring>
#include <numeric>
#include <future>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <math.h>
#include <array>

// 跨平台头文件适配
#ifdef _WIN32
#define _WIN32_WINNT 0x0600
#include <conio.h>
#include <windows.h>
#define STDIN_FILENO 0
#pragma warning(disable:4996)
#else
#include <termios.h>
#include <unistd.h>
#include <sys/ioctl.h>
#endif

using namespace std;

// 全局常量
extern const int MAX_UNDO_STEPS;
extern const int BOARD_SIZE;
extern const int TARGET;
extern const int CELL_WIDTH;
extern const int CELL_HEIGHT;
extern bool DEBUG;

// 全局映射表
extern std::map<int, int> _2PowerMap;
extern std::map<int, int> _2logMap;

// 语言枚举
enum class Language {
    ENGLISH,
    CHINESE
};

// 辅助函数声明
size_t skipAnsiCode(const string& s, size_t pos);
int calcDisplayWidth(const string& s);
int getChineseAwareWidth(const std::string& s);
std::string makestring(int length, char base);
std::string makestring(int length, std::string base);

// 跨平台键盘输入处理类
class KeyboardHandler {
private:
#ifdef _WIN32
    HANDLE hStdin;
    DWORD oldMode;
#else
    struct termios oldt, newt;
#endif
public:
    KeyboardHandler();
    ~KeyboardHandler();
    char getKey();
    bool hasKeyPressed();
};

// AI评估器类
class AIEvaluator {
private:
    unordered_map<uint64_t, pair<int, float>> transTable;

    // 预计算表
    static array<uint16_t, 65536> rowLeftTable;
    static array<uint16_t, 65536> rowRightTable;
    static array<uint64_t, 65536> colUpTable;
    static array<uint64_t, 65536> colDownTable;
    static array<float, 65536> heurScoreTable;
    static array<float, 65536> scoreTable;

    // 启发式评估参数
    static constexpr float SCORE_LOST_PENALTY = 200000.0f;
    static constexpr float SCORE_MONOTONICITY_POWER = 4.0f;
    static constexpr float SCORE_MONOTONICITY_WEIGHT = 47.0f;
    static constexpr float SCORE_SUM_POWER = 3.5f;
    static constexpr float SCORE_SUM_WEIGHT = 11.0f;
    static constexpr float SCORE_MERGES_WEIGHT = 700.0f;
    static constexpr float SCORE_EMPTY_WEIGHT = 270.0f;

    // 搜索参数
    static constexpr float CPROB_THRESH_BASE = 0.0001f;
    static constexpr int CACHE_DEPTH_LIMIT = 15;

    struct EvalState {
        unordered_map<uint64_t, pair<int, float>>& transTable;
        int maxdepth = 0;
        int curdepth = 0;
        int cachehits = 0;
        unsigned long moves_evaled = 0;
        int depth_limit = 0;

        EvalState(unordered_map<uint64_t, pair<int, float>>& table) : transTable(table) {}
    };

    static uint16_t reverseRow(uint16_t row);
    static uint64_t unpackCol(uint16_t row);
    static uint64_t transpose(uint64_t x);
    static int countEmpty(uint64_t x);
    static float scoreHelper(uint64_t board, const array<float, 65536>& table);
    static float scoreHeurBoard(uint64_t board);

    float scoreTileChooseNode(EvalState& state, uint64_t board, float cprob);
    float scoreMoveNode(EvalState& state, uint64_t board, float cprob);
    float scoreTopLevelMove(uint64_t board, int move);

public:
    // 初始化预计算表
    static void initTables();

    // 执行移动
    static uint64_t executeMove(int move, uint64_t board);

    // 评估四个方向的得分
    vector<float> evaluateAllMoves(const vector<vector<int>>& board);

    // 获取最佳移动建议
    pair<int, vector<float>> getBestMove(const vector<vector<int>>& board);

    // 棋盘表示转换函数
    static uint64_t convertToBitboard(const vector<vector<int>>& board);

    // 辅助函数：从bitboard提取行
    static inline uint16_t extractRow(uint64_t board, int row) {
        return static_cast<uint16_t>((board >> (row * 16)) & 0xFFFF);
    }

    // 辅助函数：统计不同tile数量
    static inline int countDistinctTiles(uint64_t board) {
        uint16_t bitset = 0;
        while (board) {
            bitset |= 1 << (board & 0xf);
            board >>= 4;
        }
        bitset >>= 1; // 不统计空tile

        int count = 0;
        while (bitset) {
            bitset &= bitset - 1;
            count++;
        }
        return count;
    }
};

// 2048游戏主类
class Game2048 {
private:
    // 游戏核心数据
    vector<vector<int>> board;
    vector<vector<int>> prevBoard;
    int score;
    int prevScore;
    int highScore;
    bool haveWonFlag;

    // 练习模式相关变量
    bool practiceMode;
    vector<vector<vector<int>>> practiceHistory;
    vector<int> practiceHistoryScores;
    int forcedSpawnNum;
    int forcedSpawnX;
    int forcedSpawnY;
    string spawnHint;

    // 显示相关变量
    vector<string> frameBuffer;
    vector<string> prevFrameBuffer;
    int termWidth;
    int termHeight;
    const int MIN_TERM_WIDTH;
    const int MIN_TERM_HEIGHT;

    //语言相关变量
    Language currentLanguage;
    map<string, string> chineseStrings, englishStrings;

    // AI相关变量
    AIEvaluator aiEvaluator;
    vector<float> moveScores;
    int aiBestMove;
    bool openAI;
    bool aiAutoMode;
    int aiAutoMoveDelay;
    atomic<bool> aiEvaluating;
    atomic<bool> aiCancelFlag;
    future<pair<int, vector<float>>> aiFuture;
    mutex aiMutex;

    // 键盘处理器
    KeyboardHandler keyboard;

    // 数字点阵
    std::map<int, std::vector<std::vector<int>>> numberPatterns;

public:
    // 构造函数
    Game2048();

    // 游戏主循环
    void play();

    // 获取分数
    int getScore() const { return score; }
    int getHighScore() const { return highScore; }

    std::string getString(const std::string& key);
private:
    // 语言相关函数
    void switchLanguage();
    void initLanguageStrings();

    // 游戏逻辑函数
    void initBoard();
    void addRandomTile();
    void rotateBoard();
    bool moveRowLeft(vector<int>& row);
    bool moveLeft();
    bool moveRight();
    bool moveUp();
    bool moveDown();
    bool canMove();
    bool hasWon();
    void restartGame();

    // 练习模式函数
    void enterPracticeMode();
    void savePracticeState();
    bool undoPractice();
    void handleForcedSpawnInput();

    // AI相关函数
    void cancelAIAnalysis();
    void startAsyncAIAnalysis();
    bool checkAIAnalysisResult();
    void triggerAIAnalysis();
    vector<int> softmaxScoresToPercent(const vector<float>& scores);

    // 显示函数
    void updateTerminalSize();
    bool isTerminalSizeEnough();
    void clearScreen();
    void moveCursor(int row, int col);
    string getColor(int num);
    bool isWhite(int num);
    std::vector<std::string> getLargeNumberRows(int value);
    std::string drawLargeCellLine(int value, int cellLine);
    std::string drawLargeHorizontalLine();
    std::string drawUpLargeHorizontalLine();
    std::string drawDownLargeHorizontalLine();
    void buildFrameBuffer();
    void renderFrame();
    void displayBoard();
    void resetFrameBuffer();

    // 帮助和存档函数
    void showhelp();
    bool saveGame();
    bool loadGame();
};

// 主函数声明
int main();

#endif // GAME2048_H