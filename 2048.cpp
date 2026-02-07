#include "2048.h"

// 全局常量定义
const int MAX_UNDO_STEPS = 64;
const int BOARD_SIZE = 4;
const int TARGET = 2048;
const int CELL_WIDTH = 26;
const int CELL_HEIGHT = 13;
bool DEBUG = false;

// 全局映射表定义
std::map<int, int> _2PowerMap = {
    {1,2},{2,4},{3,8},{4,16},{5,32},{6,64},{7,128},{8,256},
    {9,512},{10,1024},{11,2048},{12,4096},{13,8192},{14,16384},
    {15,32768},{16,65536}
};

std::map<int, int> _2logMap = {
    {2,1},{4,2},{8,3},{16,4},{32,5},{64,6},{128,7},{256,8},
    {512,9},{1024,10},{2048,11},{4096,12},{8192,13},{16384,14},
    {32768,15},{65536,16}
};

// AIEvaluator静态成员初始化
array<uint16_t, 65536> AIEvaluator::rowLeftTable;
array<uint16_t, 65536> AIEvaluator::rowRightTable;
array<uint64_t, 65536> AIEvaluator::colUpTable;
array<uint64_t, 65536> AIEvaluator::colDownTable;
array<float, 65536> AIEvaluator::heurScoreTable;
array<float, 65536> AIEvaluator::scoreTable;

// ==================== 辅助函数实现 ====================

// 跳过ANSI控制码
size_t skipAnsiCode(const string& s, size_t pos) {
    if (pos >= s.size() || s[pos] != '\033') return pos;
    pos++;
    while (pos < s.size() && !isalpha(s[pos]) && s[pos] != 'm') pos++;
    return pos + 1;
}

// 计算字符串在终端的实际显示列数
int calcDisplayWidth(const string& s) {
    int width = 0;
    size_t pos = 0;
    while (pos < s.size()) {
        pos = skipAnsiCode(s, pos);
        if (pos >= s.size()) break;
        unsigned char c = (unsigned char)s[pos];
        if (c >= 0x80) {
            width += 2;
            pos++;
        }
        else {
            width += 1;
        }
        pos++;
    }
    return width;
}

// 计算字符串的实际显示宽度（修正中文字符计算问题）
int getChineseAwareWidth(const std::string& s) {
    int width = 0;
    for (size_t i = 0; i < s.size();) {
        unsigned char c = static_cast<unsigned char>(s[i]);

        if (c < 0x80) {
            width += 1;
            i += 1;
        }
        else if ((c & 0xE0) == 0xC0 && i + 1 < s.size()) {
            width += 1;
            i += 2;
        }
        else if ((c & 0xF0) == 0xE0 && i + 2 < s.size()) {
            uint32_t code_point = ((c & 0x0F) << 12) |
                ((static_cast<unsigned char>(s[i + 1]) & 0x3F) << 6) |
                (static_cast<unsigned char>(s[i + 2]) & 0x3F);

            if ((code_point >= 0x4E00 && code_point <= 0x9FFF) ||
                (code_point >= 0x3400 && code_point <= 0x4DBF) ||
                (code_point >= 0x20000 && code_point <= 0x2A6DF) ||
                (code_point >= 0x2A700 && code_point <= 0x2B73F) ||
                (code_point >= 0x2B740 && code_point <= 0x2B81F) ||
                (code_point >= 0xF900 && code_point <= 0xFAFF)) {
                width += 2;
            }
            else {
                width += 1;
            }
            i += 3;
        }
        else if ((c & 0xF8) == 0xF0 && i + 3 < s.size()) {
            width += 2;
            i += 4;
        }
        else {
            width += 1;
            i += 1;
        }
    }
    return width;
}

// 生成重复字符串
std::string makestring(int length, char base) {
    std::string s = "";
    for (int i = 0; i < length; i++) s += base;
    return s;
}

std::string makestring(int length, std::string base) {
    std::string s = "";
    for (int i = 0; i < length; i++) s += base;
    return s;
}

// ==================== KeyboardHandler实现 ====================

KeyboardHandler::KeyboardHandler() {
#ifdef _WIN32
    hStdin = GetStdHandle(STD_INPUT_HANDLE);
    GetConsoleMode(hStdin, &oldMode);
    DWORD newMode = oldMode & ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
    SetConsoleMode(hStdin, newMode);
#else
    tcgetattr(STDIN_FILENO, &oldt);
    newt = oldt;
    newt.c_lflag &= ~(ICANON | ECHO);
    tcsetattr(STDIN_FILENO, TCSANOW, &newt);
#endif
}

KeyboardHandler::~KeyboardHandler() {
#ifdef _WIN32
    SetConsoleMode(hStdin, oldMode);
#else
    tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
#endif
}

char KeyboardHandler::getKey() {
#ifdef _WIN32
    return _getch();
#else
    char ch;
    read(STDIN_FILENO, &ch, 1);
    return ch;
#endif
}

bool KeyboardHandler::hasKeyPressed() {
#ifdef _WIN32
    return _kbhit() != 0;
#else
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    struct timeval tv = { 0, 0 };
    return select(STDIN_FILENO + 1, &readfds, nullptr, nullptr, &tv) > 0;
#endif
}

// ==================== AIEvaluator实现 ====================

uint16_t AIEvaluator::reverseRow(uint16_t row) {
    return (row >> 12) | ((row >> 4) & 0x00F0) | ((row << 4) & 0x0F00) | (row << 12);
}

uint64_t AIEvaluator::unpackCol(uint16_t row) {
    uint64_t tmp = row;
    return (tmp | (tmp << 12ULL) | (tmp << 24ULL) | (tmp << 36ULL)) & 0x000F000F000F000FULL;
}

uint64_t AIEvaluator::transpose(uint64_t x) {
    uint64_t a1 = x & 0xF0F00F0FF0F00F0FULL;
    uint64_t a2 = x & 0x0000F0F00000F0F0ULL;
    uint64_t a3 = x & 0x0F0F00000F0F0000ULL;
    uint64_t a = a1 | (a2 << 12) | (a3 >> 12);
    uint64_t b1 = a & 0xFF00FF0000FF00FFULL;
    uint64_t b2 = a & 0x00FF00FF00000000ULL;
    uint64_t b3 = a & 0x00000000FF00FF00ULL;
    return b1 | (b2 >> 24) | (b3 << 24);
}

int AIEvaluator::countEmpty(uint64_t x) {
    x |= (x >> 2) & 0x3333333333333333ULL;
    x |= (x >> 1);
    x = ~x & 0x1111111111111111ULL;
    x += x >> 32;
    x += x >> 16;
    x += x >> 8;
    x += x >> 4;
    return static_cast<int>(x & 0xf);
}

float AIEvaluator::scoreHelper(uint64_t board, const array<float, 65536>& table) {
    return table[(board >> 0) & 0xFFFF] +
        table[(board >> 16) & 0xFFFF] +
        table[(board >> 32) & 0xFFFF] +
        table[(board >> 48) & 0xFFFF];
}

float AIEvaluator::scoreHeurBoard(uint64_t board) {
    return scoreHelper(board, heurScoreTable) +
        scoreHelper(transpose(board), heurScoreTable);
}

// 初始化预计算表
void AIEvaluator::initTables() {
    static bool initialized = false;
    if (initialized) return;

    for (unsigned row = 0; row < 65536; ++row) {
        unsigned line[4] = {
            (row >> 0) & 0xf,
            (row >> 4) & 0xf,
            (row >> 8) & 0xf,
            (row >> 12) & 0xf
        };

        // 实际得分
        float score = 0.0f;
        for (int i = 0; i < 4; ++i) {
            int rank = line[i];
            if (rank >= 2) {
                score += (rank - 1) * (1 << rank);
            }
        }
        scoreTable[row] = score;

        // 启发式得分
        float sum = 0;
        int empty = 0;
        int merges = 0;

        int prev = 0;
        int counter = 0;
        for (int i = 0; i < 4; ++i) {
            int rank = line[i];
            sum += pow(rank, SCORE_SUM_POWER);
            if (rank == 0) {
                empty++;
            }
            else {
                if (prev == rank) {
                    counter++;
                }
                else if (counter > 0) {
                    merges += 1 + counter;
                    counter = 0;
                }
                prev = rank;
            }
        }
        if (counter > 0) {
            merges += 1 + counter;
        }

        float monotonicity_left = 0;
        float monotonicity_right = 0;
        for (int i = 1; i < 4; ++i) {
            if (line[i - 1] > line[i]) {
                monotonicity_left += pow(line[i - 1], SCORE_MONOTONICITY_POWER) - pow(line[i], SCORE_MONOTONICITY_POWER);
            }
            else {
                monotonicity_right += pow(line[i], SCORE_MONOTONICITY_POWER) - pow(line[i - 1], SCORE_MONOTONICITY_POWER);
            }
        }

        heurScoreTable[row] = SCORE_LOST_PENALTY +
            SCORE_EMPTY_WEIGHT * empty +
            SCORE_MERGES_WEIGHT * merges -
            SCORE_MONOTONICITY_WEIGHT * min(monotonicity_left, monotonicity_right) -
            SCORE_SUM_WEIGHT * sum;

        // 执行左移操作
        unsigned new_line[4] = { line[0], line[1], line[2], line[3] };
        for (int i = 0; i < 3; ++i) {
            int j;
            for (j = i + 1; j < 4; ++j) {
                if (new_line[j] != 0) break;
            }
            if (j == 4) break;

            if (new_line[i] == 0) {
                new_line[i] = new_line[j];
                new_line[j] = 0;
                i--;
            }
            else if (new_line[i] == new_line[j]) {
                if (new_line[i] != 0xf) {
                    new_line[i]++;
                }
                new_line[j] = 0;
            }
        }

        uint16_t result = (new_line[0] << 0) | (new_line[1] << 4) | (new_line[2] << 8) | (new_line[3] << 12);
        uint16_t rev_result = reverseRow(result);
        unsigned rev_row = reverseRow(static_cast<uint16_t>(row));

        rowLeftTable[row] = row ^ result;
        rowRightTable[rev_row] = rev_row ^ rev_result;
        colUpTable[row] = unpackCol(row) ^ unpackCol(result);
        colDownTable[rev_row] = unpackCol(rev_row) ^ unpackCol(rev_result);
    }

    initialized = true;
}

// 执行移动
uint64_t AIEvaluator::executeMove(int move, uint64_t board) {
    switch (move) {
    case 0: { // up
        uint64_t ret = board;
        uint64_t t = transpose(board);
        ret ^= colUpTable[(t >> 0) & 0xFFFF] << 0;
        ret ^= colUpTable[(t >> 16) & 0xFFFF] << 4;
        ret ^= colUpTable[(t >> 32) & 0xFFFF] << 8;
        ret ^= colUpTable[(t >> 48) & 0xFFFF] << 12;
        return ret;
    }
    case 1: { // down
        uint64_t ret = board;
        uint64_t t = transpose(board);
        ret ^= colDownTable[(t >> 0) & 0xFFFF] << 0;
        ret ^= colDownTable[(t >> 16) & 0xFFFF] << 4;
        ret ^= colDownTable[(t >> 32) & 0xFFFF] << 8;
        ret ^= colDownTable[(t >> 48) & 0xFFFF] << 12;
        return ret;
    }
    case 2: { // left
        uint64_t ret = board;
        ret ^= static_cast<uint64_t>(rowLeftTable[(board >> 0) & 0xFFFF]) << 0;
        ret ^= static_cast<uint64_t>(rowLeftTable[(board >> 16) & 0xFFFF]) << 16;
        ret ^= static_cast<uint64_t>(rowLeftTable[(board >> 32) & 0xFFFF]) << 32;
        ret ^= static_cast<uint64_t>(rowLeftTable[(board >> 48) & 0xFFFF]) << 48;
        return ret;
    }
    case 3: { // right
        uint64_t ret = board;
        ret ^= static_cast<uint64_t>(rowRightTable[(board >> 0) & 0xFFFF]) << 0;
        ret ^= static_cast<uint64_t>(rowRightTable[(board >> 16) & 0xFFFF]) << 16;
        ret ^= static_cast<uint64_t>(rowRightTable[(board >> 32) & 0xFFFF]) << 32;
        ret ^= static_cast<uint64_t>(rowRightTable[(board >> 48) & 0xFFFF]) << 48;
        return ret;
    }
    default:
        return ~0ULL;
    }
}

// 递归评估函数
float AIEvaluator::scoreTileChooseNode(EvalState& state, uint64_t board, float cprob) {
    if (cprob < CPROB_THRESH_BASE || state.curdepth >= state.depth_limit) {
        state.maxdepth = max(state.curdepth, state.maxdepth);
        return scoreHeurBoard(board);
    }

    if (state.curdepth < CACHE_DEPTH_LIMIT) {
        auto it = state.transTable.find(board);
        if (it != state.transTable.end()) {
            auto& entry = it->second;
            if (entry.first <= state.curdepth) {
                state.cachehits++;
                return entry.second;
            }
        }
    }

    int num_open = countEmpty(board);
    if (num_open == 0) return 0.0f;
    cprob /= num_open;

    float res = 0.0f;
    uint64_t tmp = board;
    uint64_t tile_2 = 1;
    int count = 0;

    while (tile_2 && count < num_open) {
        if ((tmp & 0xf) == 0) {
            // 90%概率生成2，10%概率生成4
            res += scoreMoveNode(state, board | tile_2, cprob * 0.9f) * 0.9f;
            res += scoreMoveNode(state, board | (tile_2 << 1), cprob * 0.1f) * 0.1f;
            count++;
        }
        tmp >>= 4;
        tile_2 <<= 4;
    }

    res = res / num_open;

    if (state.curdepth < CACHE_DEPTH_LIMIT) {
        state.transTable[board] = { state.curdepth, res };
    }

    return res;
}

float AIEvaluator::scoreMoveNode(EvalState& state, uint64_t board, float cprob) {
    float best = 0.0f;
    state.curdepth++;

    for (int move = 0; move < 4; ++move) {
        uint64_t newboard = executeMove(move, board);
        state.moves_evaled++;

        if (board != newboard) {
            best = max(best, scoreTileChooseNode(state, newboard, cprob));
        }
    }

    state.curdepth--;
    return best;
}

float AIEvaluator::scoreTopLevelMove(uint64_t board, int move) {
    uint64_t newboard = executeMove(move, board);

    if (board == newboard) return 0.0f;

    EvalState state(transTable);
    state.depth_limit = max(3, countDistinctTiles(board) - 2);

    return scoreTileChooseNode(state, newboard, 1.0f) + 1e-6;
}

// 棋盘表示转换函数
uint64_t AIEvaluator::convertToBitboard(const vector<vector<int>>& board) {
    uint64_t bitboard = 0;

    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            int value = board[i][j];
            int tile = 0;

            if (value > 0) {
                tile = static_cast<int>(_2logMap[value]);
                if (tile > 15) tile = 15;
            }

            int shift = (i * 4 + j) * 4;
            bitboard |= static_cast<uint64_t>(tile) << shift;
        }
    }

    return bitboard;
}

// 评估四个方向的得分
vector<float> AIEvaluator::evaluateAllMoves(const vector<vector<int>>& board) {
    initTables();
    transTable.clear();

    uint64_t bitboard = convertToBitboard(board);
    vector<float> scores(4, 0.0f);

    for (int move = 0; move < 4; move++) {
        scores[move] = scoreTopLevelMove(bitboard, move);
    }

    return scores;
}

// 获取最佳移动建议
pair<int, vector<float>> AIEvaluator::getBestMove(const vector<vector<int>>& board) {
    vector<float> scores = evaluateAllMoves(board);

    int bestMove = -1;
    float bestScore = -1.0f;

    for (int i = 0; i < 4; i++) {
        if (scores[i] > bestScore) {
            bestScore = scores[i];
            bestMove = i;
        }
    }

    return { bestMove, scores };
}

// ==================== Game2048实现 ====================

Game2048::Game2048() :
    MIN_TERM_WIDTH(BOARD_SIZE* CELL_WIDTH + (BOARD_SIZE - 1) + 4),
    MIN_TERM_HEIGHT(6 + BOARD_SIZE * (CELL_HEIGHT + 1) + 3) {

    srand(time(0));
    score = 0;
    prevScore = -1;
    highScore = 0;
    haveWonFlag = false;
    practiceMode = false;
    forcedSpawnNum = 0;
    forcedSpawnX = -1;
    forcedSpawnY = -1;
    spawnHint = "";

    // 初始化AI相关变量
    moveScores = vector<float>(4, 0.0f);
    aiBestMove = -1;
    openAI = false;
    aiAutoMode = false;
    aiAutoMoveDelay = 0;
    aiEvaluating = false;
    aiCancelFlag = false;

    // 初始化数字点阵
    numberPatterns = {
        {0, {{1,1,1},{1,0,1},{1,0,1},{1,0,1},{1,1,1}}},
        {1, {{0,1,0},{1,1,0},{0,1,0},{0,1,0},{1,1,1}}},
        {2, {{1,1,1},{0,0,1},{1,1,1},{1,0,0},{1,1,1}}},
        {3, {{1,1,1},{0,0,1},{1,1,1},{0,0,1},{1,1,1}}},
        {4, {{1,0,1},{1,0,1},{1,1,1},{0,0,1},{0,0,1}}},
        {5, {{1,1,1},{1,0,0},{1,1,1},{0,0,1},{1,1,1}}},
        {6, {{1,1,1},{1,0,0},{1,1,1},{1,0,1},{1,1,1}}},
        {7, {{1,1,1},{0,0,1},{0,0,1},{0,0,1},{0,0,1}}},
        {8, {{1,1,1},{1,0,1},{1,1,1},{1,0,1},{1,1,1}}},
        {9, {{1,1,1},{1,0,1},{1,1,1},{0,0,1},{1,1,1}}}
    };

    initBoard();
    updateTerminalSize();
    resetFrameBuffer();
}

// 跨平台清屏
void Game2048::clearScreen() {
#ifdef _WIN32
    system("cls");
#else
    system("clear");
#endif
}

// 移动光标
void Game2048::moveCursor(int row, int col) {
    std::cout << "\033[" << (row + 1) << ";" << (col + 1) << "H";
}

// 跨平台获取终端当前尺寸
void Game2048::updateTerminalSize() {
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    HANDLE hStdout = GetStdHandle(STD_OUTPUT_HANDLE);
    if (GetConsoleScreenBufferInfo(hStdout, &csbi)) {
        termWidth = csbi.dwSize.X;
        termHeight = csbi.dwSize.Y;
    }
    else {
        termWidth = 120;
        termHeight = 60;
    }
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
        termWidth = ws.ws_col;
        termHeight = ws.ws_row;
    }
    else {
        termWidth = 120;
        termHeight = 60;
    }
#endif
}

// 校验终端尺寸是否满足显示要求
bool Game2048::isTerminalSizeEnough() {
    return termWidth >= MIN_TERM_WIDTH && termHeight >= MIN_TERM_HEIGHT;
}

// 初始化棋盘
void Game2048::initBoard() {
    board.resize(BOARD_SIZE, vector<int>(BOARD_SIZE, 0));
    prevBoard.resize(BOARD_SIZE, vector<int>(BOARD_SIZE, -1));
    score = 0;
    prevScore = -1;
    practiceMode = false;
    practiceHistory.clear();
    practiceHistoryScores.clear();
    forcedSpawnNum = 0;
    forcedSpawnX = -1;
    forcedSpawnY = -1;
    spawnHint = "";

    if (DEBUG) {
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                int j_ = j;
                if (i % 2) { j_ = BOARD_SIZE - j - 1; }
                board[i][j] = _2PowerMap[i * BOARD_SIZE + j_ + 1];
            }
        }
        board[0][0] = 4;
    }
    addRandomTile();
    addRandomTile();

    // 初始化AI评估状态
    aiEvaluating = false;
    aiCancelFlag = false;
    moveScores = vector<float>(4, 0.0f);
    aiBestMove = -1;
}

// 随机生成数字
void Game2048::addRandomTile() {
    // 先检查是否有强制生成的要求
    if (practiceMode && forcedSpawnNum != 0 && forcedSpawnX >= 0 && forcedSpawnY >= 0) {
        if (board[forcedSpawnX][forcedSpawnY] == 0) {
            board[forcedSpawnX][forcedSpawnY] = forcedSpawnNum;
            forcedSpawnNum = 0;
            forcedSpawnX = -1;
            forcedSpawnY = -1;
            spawnHint = "";
            return;
        }
        else {
            vector<pair<int, int>> emptyCells;
            for (int i = 0; i < BOARD_SIZE; i++)
                for (int j = 0; j < BOARD_SIZE; j++)
                    if (board[i][j] == 0) emptyCells.emplace_back(i, j);
            if (!emptyCells.empty()) {
                int idx = rand() % emptyCells.size();
                board[emptyCells[idx].first][emptyCells[idx].second] = forcedSpawnNum;
                forcedSpawnNum = 0;
                forcedSpawnX = -1;
                forcedSpawnY = -1;
                spawnHint = "";
                return;
            }
        }
    }

    // 常规随机生成逻辑
    vector<pair<int, int>> emptyCells;
    for (int i = 0; i < BOARD_SIZE; i++)
        for (int j = 0; j < BOARD_SIZE; j++)
            if (board[i][j] == 0) emptyCells.emplace_back(i, j);
    if (!emptyCells.empty()) {
        int idx = rand() % emptyCells.size();
        board[emptyCells[idx].first][emptyCells[idx].second] = (rand() % 10 == 0) ? 4 : 2;
    }
    spawnHint = "";
}

// 矩阵旋转
void Game2048::rotateBoard() {
    vector<vector<int>> rotated(BOARD_SIZE, vector<int>(BOARD_SIZE));
    for (int i = 0; i < BOARD_SIZE; i++)
        for (int j = 0; j < BOARD_SIZE; j++)
            rotated[j][BOARD_SIZE - 1 - i] = board[i][j];
    board = std::move(rotated);
}

// 行左移
bool Game2048::moveRowLeft(vector<int>& row) {
    vector<int> newRow(BOARD_SIZE, 0);
    int idx = 0;
    bool merged = false;
    for (int num : row) {
        if (num == 0) continue;
        if (newRow[idx] == 0) {
            newRow[idx] = num;
        }
        else if (newRow[idx] == num) {
            newRow[idx] *= 2;
            score += newRow[idx];
            if (score > highScore) highScore = score;
            idx++;
            merged = true;
        }
        else {
            idx++;
            newRow[idx] = num;
        }
    }
    if (row != newRow) {
        row = std::move(newRow);
        return true;
    }
    return merged;
}

// 四个方向移动
bool Game2048::moveLeft() { bool ok = false; for (auto& r : board) if (moveRowLeft(r)) ok = true; return ok; }
bool Game2048::moveRight() { rotateBoard(); rotateBoard(); bool ok = moveLeft(); rotateBoard(); rotateBoard(); return ok; }
bool Game2048::moveUp() { rotateBoard(); rotateBoard(); rotateBoard(); bool ok = moveLeft(); rotateBoard(); return ok; }
bool Game2048::moveDown() { rotateBoard(); bool ok = moveLeft(); rotateBoard(); rotateBoard(); rotateBoard(); return ok; }

// 检查可移动
bool Game2048::canMove() {
    for (int i = 0; i < BOARD_SIZE; i++)
        for (int j = 0; j < BOARD_SIZE; j++)
            if (board[i][j] == 0) return true;
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (j < BOARD_SIZE - 1 && board[i][j] == board[i][j + 1]) return true;
            if (i < BOARD_SIZE - 1 && board[i][j] == board[i + 1][j]) return true;
        }
    }
    return false;
}

// 检查是否获胜
bool Game2048::hasWon() {
    if (haveWonFlag) return false;
    haveWonFlag = true;
    for (auto& r : board) for (int num : r) if (num == TARGET) return true;
    return false;
}

// 颜色码
string Game2048::getColor(int num) {
    if (num == 0) return "\033[48;5;235m\033[38;5;235m";
    if (num == 2) return "\033[48;5;255m\033[38;5;0m";
    if (num == 4) return "\033[48;5;230m\033[38;5;0m";
    if (num == 8) return "\033[48;5;215m\033[38;5;255m";
    if (num == 16) return "\033[48;5;209m\033[38;5;255m";
    if (num == 32) return "\033[48;5;203m\033[38;5;255m";
    if (num == 64) return "\033[48;5;196m\033[38;5;255m";
    if (num == 128) return "\033[48;5;220m\033[38;5;0m";
    if (num == 256) return "\033[48;5;214m\033[38;5;255m";
    if (num == 512) return "\033[48;5;178m\033[38;5;255m";
    if (num == 1024) return "\033[48;5;172m\033[38;5;255m";
    if (num == 2048) return "\033[48;5;166m\033[38;5;255m";
    if (num == 4096) return "\033[48;5;93m\033[38;5;255m";
    if (num == 8192) return "\033[48;5;57m\033[38;5;255m";
    if (num == 16384) return "\033[48;5;21m\033[38;5;255m";
    if (num == 32768) return "\033[48;5;27m\033[38;5;255m";
    if (num == 65536) return "\033[48;5;233m\033[38;5;255m";
    return "\033[48;5;0m\033[38;5;255m";
}

bool Game2048::isWhite(int num) { return (num >= 8 && num != 128); }

// 生成数字点阵
std::vector<std::string> Game2048::getLargeNumberRows(int value) {
    std::vector<std::string> rows(5, "");
    if (value == 0) return rows;
    std::string digits = to_string(value);
    if (value == 72) digits = '0' + digits;
    int digitCount = digits.length();
    std::vector<std::vector<std::string>> digitRows(digitCount, std::vector<std::string>(5, ""));
    for (int d = 0; d < digitCount; d++) {
        int digit = digits[d] - '0';
        if (!numberPatterns.count(digit)) continue;
        const auto& pattern = numberPatterns[digit];
        for (int row = 0; row < 5; row++) {
            for (int col = 0; col < 3; col++) {
                if (pattern[row][col]) {
                    digitRows[d][row] += 'c';
                    digitRows[d][row] += "██";
                    digitRows[d][row] += 'r';
                }
                else {
                    digitRows[d][row] += "  ";
                }
            }
        }
    }
    for (int row = 0; row < 5; row++) {
        for (int d = 0; d < digitCount; d++) {
            rows[row] += digitRows[d][row];
            if (d < digitCount - 1) rows[row] += "  ";
        }
    }
    return rows;
}

// 绘制单元格单行
std::string Game2048::drawLargeCellLine(int value, int cellLine) {
    std::string line;
    if (value < 1000) {
        if (cellLine == 0) line = "┌" + makestring(CELL_WIDTH - 2, "─") + "┐";
        else if (cellLine == CELL_HEIGHT - 1) line = "└" + makestring(CELL_WIDTH - 2, "─") + "┘";
        else if (cellLine <= 3 || cellLine == CELL_HEIGHT - 4) line = "│" + makestring(CELL_WIDTH - 2, ' ') + "│";
        else {
            std::vector<std::string> numberRows = getLargeNumberRows(value);
            int numberRow = cellLine - 4;
            if (numberRow >= 0 && numberRow < 5) {
                std::string numberLine = numberRows[numberRow];
                int lineWidth = value > 0 ? 6 : 0;
                if (value >= 10) lineWidth = value >= 100 ? 22 : 14;
                if (value == 72) lineWidth = 22;
                int padding = (CELL_WIDTH - 2 - lineWidth) / 2;
                line = "│" + makestring(padding, ' ') + numberLine + makestring(CELL_WIDTH - 2 - lineWidth - padding, ' ') + "│";
            }
            else line = "│" + makestring(CELL_WIDTH - 2, ' ') + "│";
        }
    }
    else if (value < 100000) {
        if (cellLine == 0) line = "┌" + makestring(CELL_WIDTH - 2, "─") + "┐";
        else if (cellLine == CELL_HEIGHT - 1) line = "└" + makestring(CELL_WIDTH - 2, "─") + "┘";
        else if (cellLine >= 6 && cellLine < CELL_HEIGHT - 6) line = "│" + makestring(CELL_WIDTH - 2, ' ') + "│";
        else if (cellLine <= 6) line = drawLargeCellLine(value / 100, cellLine + 3);
        else line = drawLargeCellLine(value % 100, cellLine - 3);
    }
    else {
        if (cellLine == 0) line = "┌" + makestring(CELL_WIDTH - 2, "─") + "┐";
        else if (cellLine == CELL_HEIGHT - 1) line = "└" + makestring(CELL_WIDTH - 2, "─") + "┘";
        else if (cellLine >= 6 && cellLine < CELL_HEIGHT - 6) line = "│" + makestring(CELL_WIDTH - 2, ' ') + "│";
        else if (cellLine <= 6) line = drawLargeCellLine(value / 1000, cellLine + 3);
        else line = drawLargeCellLine(value % 1000, cellLine - 3);
    }
    return line;
}

// 绘制分隔线
std::string Game2048::drawLargeHorizontalLine() {
    std::string line = "├";
    for (int i = 0; i < BOARD_SIZE; i++) line += makestring(CELL_WIDTH, "─") + (i < BOARD_SIZE - 1 ? "┼" : "┤");
    return line;
}

std::string Game2048::drawUpLargeHorizontalLine() {
    std::string line = "├";
    for (int i = 0; i < BOARD_SIZE; i++) line += makestring(CELL_WIDTH, "─") + (i < BOARD_SIZE - 1 ? "┬" : "┤");
    return line;
}

std::string Game2048::drawDownLargeHorizontalLine() {
    std::string line = "└";
    for (int i = 0; i < BOARD_SIZE; i++) line += makestring(CELL_WIDTH, "─") + (i < BOARD_SIZE - 1 ? "┴" : "┘");
    return line;
}

// 练习模式：保存当前状态到历史记录
void Game2048::savePracticeState() {
    if (practiceMode) {
        practiceHistory.push_back(board);
        practiceHistoryScores.push_back(score);

        if (practiceHistory.size() > MAX_UNDO_STEPS) {
            practiceHistory.erase(practiceHistory.begin());
            practiceHistoryScores.erase(practiceHistoryScores.begin());
        }
    }
}

// 练习模式下撤销
bool Game2048::undoPractice() {
    if (!practiceMode || practiceHistory.size() <= 1) {
        return false;
    }

    practiceHistory.pop_back();
    practiceHistoryScores.pop_back();

    board = practiceHistory.back();
    score = practiceHistoryScores.back();

    return true;
}

// 取消AI评估
void Game2048::cancelAIAnalysis() {
    if (aiEvaluating && aiFuture.valid()) {
        aiCancelFlag = true;
        if (aiFuture.wait_for(chrono::milliseconds(50)) == future_status::ready) {
            try {
                aiFuture.get();
            }
            catch (...) {
                // 忽略异常
            }
        }
        aiEvaluating = false;
    }
}

// 异步启动AI评估
void Game2048::startAsyncAIAnalysis() {
    if (aiEvaluating) {
        cancelAIAnalysis();
    }

    aiEvaluating = true;
    aiCancelFlag = false;

    vector<vector<int>> currentBoard = board;

    packaged_task<pair<int, vector<float>>()> task([this, currentBoard]() -> pair<int, vector<float>> {
        AIEvaluator localEvaluator;
        return localEvaluator.getBestMove(currentBoard);
        });

    aiFuture = task.get_future();

    thread([this, task = std::move(task)]() mutable {
        task();
        }).detach();
}

// 检查AI评估是否完成并获取结果
bool Game2048::checkAIAnalysisResult() {
    if (!aiEvaluating || !aiFuture.valid()) {
        return false;
    }

    auto status = aiFuture.wait_for(chrono::milliseconds(0));
    if (status != future_status::ready) {
        return false;
    }

    try {
        auto result = aiFuture.get();
        {
            lock_guard<mutex> lock(aiMutex);
            if (!aiCancelFlag) {
                aiBestMove = result.first;
                moveScores = result.second;
            }
        }
        aiEvaluating = false;
        return true;
    }
    catch (const future_error& e) {
        aiEvaluating = false;
        return false;
    }
    catch (...) {
        aiEvaluating = false;
        return false;
    }
}

void Game2048::triggerAIAnalysis() {
    if (openAI && canMove()) {
        startAsyncAIAnalysis();
    }
    else if (openAI) {
        {
            lock_guard<mutex> lock(aiMutex);
            aiBestMove = -1;
            moveScores = vector<float>(4, 0.0f);
        }
        aiEvaluating = false;
    }
}

// 使用softmax计算相对权重
vector<int> Game2048::softmaxScoresToPercent(const vector<float>& scores) {
    vector<int> percentages(4, 0);

    float maxScore = scores[0];
    for (int i = 1; i < 4; i++) {
        if (scores[i] > maxScore) maxScore = scores[i];
    }

    vector<float> expScores(4);
    float sumExp = 0.0f;
    for (int i = 0; i < 4; i++) {
        expScores[i] = expf((scores[i] - maxScore) / 1000.0f);
        sumExp += expScores[i];
    }

    int remaining = 100;
    int assigned = 0;
    for (int i = 0; i < 4; i++) {
        int percent = static_cast<int>((expScores[i] / sumExp) * 100 + 0.5f);
        percentages[i] = percent;
        assigned += percent;
    }

    if (assigned != 100) {
        int diff = 100 - assigned;
        percentages[0] += diff;
    }

    return percentages;
}

// 构建完整的帧缓存
void Game2048::buildFrameBuffer() {
    frameBuffer.clear();
    frameBuffer.resize(termHeight, "");
    int totalWidth = BOARD_SIZE * CELL_WIDTH + (BOARD_SIZE - 1) + 2;
    int lineIdx = 0;
    ostringstream oss;

    // 绘制标题栏
    frameBuffer[lineIdx++] = "";

    oss.str(""); oss << "┌" << makestring(totalWidth - 2, "─") << "┐";
    frameBuffer[lineIdx++] = oss.str();

    string title = "2048";
    oss.str("");
    int titlePadding = (totalWidth - 2 - title.length()) / 2;
    oss << "│" << makestring(titlePadding, ' ') << title << makestring(totalWidth - 2 - title.length() - titlePadding, ' ') << "│";
    frameBuffer[lineIdx++] = oss.str();

    // 绘制分数栏
    int maxNum = 0;
    for (auto& r : board) for (int num : r) if (num > maxNum) maxNum = num;
    string scoreStr = "当前分数: " + to_string(score);
    string maxNumStr = "当前最大数字: " + (maxNum > 0 ? to_string(maxNum) : "0");
    int scoreWidth = scoreStr.length() - 4;
    int maxNumWidth = maxNumStr.length() - 6;
    int availableWidth = totalWidth - 4;
    int middleSpace = availableWidth - scoreWidth - maxNumWidth;
    if (middleSpace < 0) middleSpace = 0;
    oss.str(""); oss << "│ " << scoreStr << makestring(middleSpace, ' ') << maxNumStr << " │";
    frameBuffer[lineIdx++] = oss.str();

    // 练习模式提示
    string practiceHint = "";
    if (practiceMode) {
        practiceHint = "练习模式: 按Z撤销 | 按K指定生成位置";
    }
    if (!practiceHint.empty()) {
        oss.str("");
        int hintPadding = (totalWidth - 2 - getChineseAwareWidth(practiceHint)) / 2;
        if (hintPadding < 0) hintPadding = 0;
        oss << "│" << makestring(hintPadding, ' ') << practiceHint << makestring(totalWidth - 2 - getChineseAwareWidth(practiceHint) - hintPadding, ' ') << "│";
        frameBuffer[lineIdx++] = oss.str();
    }
    else {
        oss.str(""); oss << "│" << makestring(totalWidth - 2, ' ') << "│";
        frameBuffer[lineIdx++] = oss.str();
    }

    // 显示AI评估信息
    if (openAI) {
        oss.str("");
        vector<string> moveNames = { "上", "下", "左", "右" };

        if (aiAutoMode) {
            oss << "\033[1;32m(AI自动模式";
            oss << "运行中\033[0m) ";
        }

        if (aiEvaluating && !aiAutoMode) {
            oss << "AI评估: 计算中...";
        }
        else {
            oss << "AI评估: ";
            lock_guard<mutex> lock(aiMutex);
            std::vector<int> percentages = softmaxScoresToPercent(moveScores);

            for (int i = 0; i < 4; i++) {
                if (moveScores[i] > 0.0f) {
                    if (i == aiBestMove && aiBestMove >= 0) {
                        oss << "\033[1;32m" << moveNames[i];
                    }
                    else {
                        oss << moveNames[i];
                    }
                    string movescoreStr = "(" + to_string(percentages[i]) + ")";

                    if (DEBUG) {
                        char movescoreStrOrigin[20];
                        if (moveScores[i] < 10.0f) {
                            snprintf(movescoreStrOrigin, sizeof(movescoreStrOrigin), "%.3f", moveScores[i]);
                        }
                        else if (moveScores[i] < 100.0f) {
                            snprintf(movescoreStrOrigin, sizeof(movescoreStrOrigin), "%.2f", moveScores[i]);
                        }
                        else {
                            snprintf(movescoreStrOrigin, sizeof(movescoreStrOrigin), "%.1f", moveScores[i]);
                        }
                        movescoreStr = string("(") + string(movescoreStrOrigin) + string(")");
                    }

                    if (i == aiBestMove && aiBestMove >= 0) {
                        oss << "\033[1;32m" << movescoreStr << "\033[0m";
                    }
                    else {
                        oss << movescoreStr;
                    }

                    if (i < 3) oss << " ";
                }
            }
        }

        string aiStr = oss.str();
        int aiWidth = getChineseAwareWidth(aiStr) - (aiAutoMode ? 29 : (aiEvaluating ? 0 : 18));
        int aiPadding = (totalWidth - aiWidth) / 2;
        if (aiPadding < 0) aiPadding = 0;
        oss.str("");
        oss << "│" << makestring(aiPadding, ' ') << aiStr << makestring(totalWidth - aiWidth - aiPadding - 2, ' ') << "│";

        frameBuffer[lineIdx++] = oss.str();
    }
    else {
        oss.str("");
        oss << "│" << makestring(totalWidth - 2, ' ') << "│";
        frameBuffer[lineIdx++] = oss.str();
    }

    // 绘制棋盘
    frameBuffer[lineIdx++] = drawUpLargeHorizontalLine();
    for (int row = 0; row < BOARD_SIZE; row++) {
        for (int cellLine = 0; cellLine < CELL_HEIGHT; cellLine++) {
            oss.str(""); oss << "│";
            for (int col = 0; col < BOARD_SIZE; col++) {
                int val = board[row][col];
                oss << getColor(val);
                string line = drawLargeCellLine(val, cellLine);
                for (auto chr : line) {
                    if (chr == 'c') oss << (isWhite(val) ? "\033[48;5;255m\033[38;5;255m" : "\033[48;5;0m\033[38;5;0m");
                    else if (chr == 'r') oss << getColor(val);
                    else oss << chr;
                }
                oss << "\033[0m";
                if (col < BOARD_SIZE - 1) oss << "│";
            }
            oss << "│";
            frameBuffer[lineIdx++] = oss.str();
            if (lineIdx >= termHeight) break;
        }
        if (lineIdx >= termHeight) break;
        if (row < BOARD_SIZE - 1) frameBuffer[lineIdx++] = drawLargeHorizontalLine();
    }
    if (lineIdx < termHeight) frameBuffer[lineIdx++] = drawDownLargeHorizontalLine();

    // 绘制获胜提示
    if (hasWon() && lineIdx < termHeight) {
        frameBuffer[lineIdx++] = "\n✨🎉✨ 恭喜！你已经达到 " + to_string(TARGET) + "！可以继续游戏！ ✨🎉✨";
    }

    // 显示强制生成提示
    if (!spawnHint.empty() && lineIdx < termHeight) {
        int hintPad = (totalWidth - calcDisplayWidth(spawnHint)) / 2;
        if (hintPad < 0) hintPad = 0;
        frameBuffer[lineIdx++] = makestring(hintPad, ' ') + spawnHint;
    }

    // 终端尺寸不足时，绘制警告信息
    if (!isTerminalSizeEnough()) {
        frameBuffer.clear();
        frameBuffer.resize(termHeight, "");
        string warn1 = "\033[31m⚠️  终端尺寸不足！最小要求：宽" + to_string(MIN_TERM_WIDTH) + " 高" + to_string(MIN_TERM_HEIGHT) + " ⚠️\033[0m";
        string warn2 = "\033[31m请放大终端窗口后，按任意键重绘...（windows系统可以按ctrl+滚轮缩放终端）\033[0m";
        int warnPad1 = (termWidth - calcDisplayWidth(warn1)) / 2;
        int warnPad2 = (termWidth - calcDisplayWidth(warn2)) / 2;
        if (warnPad1 < 0) warnPad1 = 0;
        if (warnPad2 < 0) warnPad2 = 0;
        frameBuffer[termHeight / 2 - 1] = makestring(warnPad1, ' ') + warn1;
        frameBuffer[termHeight / 2] = makestring(warnPad2, ' ') + warn2;
    }

    // 补全帧缓存
    for (int i = 0; i < termHeight; i++) {
        int currWidth = calcDisplayWidth(frameBuffer[i]);
        if (currWidth < termWidth) {
            frameBuffer[i] += makestring(termWidth - currWidth, ' ');
        }
    }
}

// 帧缓存对比 + 增量更新屏幕
void Game2048::renderFrame() {
    if (prevFrameBuffer.empty() || !isTerminalSizeEnough() || prevFrameBuffer.size() != frameBuffer.size()) {
        clearScreen();
        for (int i = 0; i < termHeight; i++) {
            cout << frameBuffer[i] << flush;
            if (i < termHeight - 1) cout << "\n";
        }
    }
    else {
        for (int i = 0; i < termHeight; i++) {
            if (frameBuffer[i] != prevFrameBuffer[i]) {
                moveCursor(i, 0);
                cout << frameBuffer[i] << flush;
            }
        }
    }
    prevFrameBuffer = frameBuffer;
    moveCursor(termHeight, 0);
    cout << flush;
}

// 显示方法入口
void Game2048::displayBoard() {
    buildFrameBuffer();
    renderFrame();
}

// 重置帧缓存
void Game2048::resetFrameBuffer() {
    prevFrameBuffer.clear();
    frameBuffer.clear();
}

// 重新开始游戏
void Game2048::restartGame() {
    board.assign(BOARD_SIZE, vector<int>(BOARD_SIZE, 0));
    prevBoard.assign(BOARD_SIZE, vector<int>(BOARD_SIZE, -1));
    score = 0;
    prevScore = -1;
    haveWonFlag = false;
    practiceMode = false;
    practiceHistory.clear();
    practiceHistoryScores.clear();
    forcedSpawnNum = 0;
    forcedSpawnX = -1;
    forcedSpawnY = -1;
    spawnHint = "";
    addRandomTile();
    addRandomTile();

    cancelAIAnalysis();
    moveScores = vector<float>(4, 0.0f);
    aiBestMove = -1;

    triggerAIAnalysis();
    resetFrameBuffer();
}

// 练习模式：进入练习模式
void Game2048::enterPracticeMode() {
    vector<vector<int>> savedBoard = board;
    int savedScore = score;
    int savedForcedNum = forcedSpawnNum;
    int savedForcedX = forcedSpawnX;
    int savedForcedY = forcedSpawnY;
    string savedSpawnHint = spawnHint;

    clearScreen();
    cout << "\n══════════════════════════════════════════════════════\n";
    cout << "                   练习模式                          \n";
    cout << "══════════════════════════════════════════════════════\n\n";
    cout << "请输入一个4x4的局面，每个位置输入0-16的数字：\n";
    cout << "  0表示空位，1表示2，2表示4，...，16表示65536\n";
    cout << "  输入示例：第一行: 0 0 0 0，第二行: 0 2 0 0\n";
    cout << "  输入-1取消并返回原局面\n\n";

    vector<vector<int>> newBoard(BOARD_SIZE, vector<int>(BOARD_SIZE, 0));
    int value;
    bool cancel = false;

    for (int i = 0; i < BOARD_SIZE; i++) {
        cout << "第" << (i + 1) << "行（4个数字，空格分隔）: ";
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (!(cin >> value)) {
                cin.clear();
                cin.ignore(10000, '\n');
                cout << "输入格式错误！\n";
                cancel = true;
                break;
            }

            if (value == -1) {
                cancel = true;
                break;
            }

            if (value < 0 || value > 16) {
                cout << "错误：数字必须在0-16之间！\n";
                cancel = true;
                break;
            }

            if (value == 0) {
                newBoard[i][j] = 0;
            }
            else {
                newBoard[i][j] = _2PowerMap[value];
            }
        }

        if (cancel) break;
        cin.ignore(10000, '\n');
    }

    if (!cancel) {
        bool hasNonZero = false;
        for (int i = 0; i < BOARD_SIZE; i++) {
            for (int j = 0; j < BOARD_SIZE; j++) {
                if (newBoard[i][j] != 0) {
                    hasNonZero = true;
                    break;
                }
            }
            if (hasNonZero) break;
        }

        if (!hasNonZero) {
            cout << "\n错误：局面不能全为空！\n";
            cancel = true;
        }
    }

    if (cancel) {
        board = savedBoard;
        score = savedScore;
        forcedSpawnNum = savedForcedNum;
        forcedSpawnX = savedForcedX;
        forcedSpawnY = savedForcedY;
        spawnHint = savedSpawnHint;
        cout << "\n已取消练习模式，返回原局面。\n";
        cout << "\n按任意键继续..." << flush;
    }
    else {
        board = newBoard;
        score = 0;
        practiceMode = true;
        practiceHistory.clear();
        practiceHistoryScores.clear();
        forcedSpawnNum = 0;
        forcedSpawnX = -1;
        forcedSpawnY = -1;
        spawnHint = "";

        practiceHistory.push_back(board);
        practiceHistoryScores.push_back(score);

        cout << "\n已进入练习模式！\n";
        cout << "  • 按Z键撤销到上一个局面\n";
        cout << "  • 按K键指定下一次生成的数字和位置\n";
        cout << "  • 按R键重新开始游戏将退出练习模式\n";
        cout << "\n按任意键继续..." << flush;
    }

    KeyboardHandler tempKB;
    tempKB.getKey();
    resetFrameBuffer();
}

// 处理强制生成的输入
void Game2048::handleForcedSpawnInput() {
    if (!practiceMode) return;
    spawnHint = "";

    int inputRow = termHeight;
    moveCursor(inputRow, 0);
    cout << "\033[K" << "请输入强制生成参数（数字 行 列，用空格分隔，按Enter确认）：" << flush;

    KeyboardHandler* kbPtr = reinterpret_cast<KeyboardHandler*>(&keyboard);
    kbPtr->~KeyboardHandler();

    int num, x, y;
    cin >> num >> x >> y;
    //cin.ignore(numeric_limits<streamsize>::max(), '\n');

    bool valid = true;
    if (num != 2 && num != 4) {
        valid = false;
        spawnHint = "\033[31m输入错误：第一个数必须是2或4！\033[0m";
    }
    else if (x < 1 || x > 4 || y < 1 || y > 4) {
        valid = false;
        spawnHint = "\033[31m输入错误：行和列必须是1-4之间的数字！\033[0m";
    }

    if (valid) {
        forcedSpawnNum = num;
        forcedSpawnX = x - 1;
        forcedSpawnY = y - 1;
        spawnHint = "\033[33m下次将生成" + to_string(num) + " 在第" + to_string(x) + "行第" + to_string(y) + "列\033[0m";
    }

    new (kbPtr) KeyboardHandler();
    resetFrameBuffer();
    displayBoard();
}

// 帮助界面
void Game2048::showhelp() {
    clearScreen();
    std::ostringstream oss;
    int totalWidth = BOARD_SIZE * CELL_WIDTH + (BOARD_SIZE - 1) + 2;
    oss << "┌" << makestring(totalWidth - 2, "─") << "┐\n";
    string controlTitle = "游戏控制";
    int controlTitleWidth = getChineseAwareWidth(controlTitle);
    int controlTitlePadding = (totalWidth - 2 - controlTitleWidth) / 2;
    oss << "│" << makestring(controlTitlePadding, ' ') << controlTitle << makestring(totalWidth - 2 - controlTitleWidth - controlTitlePadding, ' ') << "│\n";
    oss << "├" << makestring(totalWidth - 2, "─") << "┤\n";
    string moveStr = "方向键 (↑ ↓ ← →) 或 WASD 键移动方块";
    string controlStr = "Q 键 - 退出游戏    R 键 - 重新开始";
    string saveLoadStr = "M 键 - 保存游戏    L 键 - 读取存档";
    string practiceStr = "P 键 - 练习模式    Z 键 - 练习模式下撤销    K 键 - 练习模式指定生成位置";
    string aiStr = "I 键 - 切换AI评估显示    0 键 - 开启/关闭AI自动模式";

    int moveWidth = getChineseAwareWidth(moveStr);
    int controlWidth = getChineseAwareWidth(controlStr);
    int saveLoadWidth = getChineseAwareWidth(saveLoadStr);
    int practiceWidth = getChineseAwareWidth(practiceStr);
    int aiWidth = getChineseAwareWidth(aiStr);

    int movePadding = (totalWidth - 2 - moveWidth) / 2;
    int controlPadding = (totalWidth - 2 - controlWidth) / 2;
    int saveLoadPadding = (totalWidth - 2 - saveLoadWidth) / 2;
    int practicePadding = (totalWidth - 2 - practiceWidth) / 2;
    int aiPadding = (totalWidth - 2 - aiWidth) / 2;

    oss << "│" << makestring(movePadding, ' ') << moveStr << makestring(totalWidth - 2 - moveWidth - movePadding, ' ') << "│\n";
    oss << "│" << makestring(controlPadding, ' ') << controlStr << makestring(totalWidth - 2 - controlWidth - controlPadding, ' ') << "│\n";
    oss << "│" << makestring(saveLoadPadding, ' ') << saveLoadStr << makestring(totalWidth - 2 - saveLoadWidth - saveLoadPadding, ' ') << "│\n";
    oss << "│" << makestring(practicePadding, ' ') << practiceStr << makestring(totalWidth - 2 - practiceWidth - practicePadding, ' ') << "│\n";
    oss << "│" << makestring(aiPadding, ' ') << aiStr << makestring(totalWidth - 2 - aiWidth - aiPadding, ' ') << "│\n";
    oss << "└" << makestring(totalWidth - 2, "─") << "┘\n\n";
    cout << oss.str() << flush;
    cout << "按Enter键继续...\n" << flush;
    resetFrameBuffer();
}

// 保存游戏
bool Game2048::saveGame() {
    clearScreen();
    cout << "\n══════════════════════════════════════════════════════\n";
    cout << "                   保存游戏                          \n";
    cout << "══════════════════════════════════════════════════════\n\n";
    cout << "是否保存当前游戏进度？(y/n): ";
    char confirm;
    cin >> confirm;
    cin.ignore();
    if (tolower(confirm) != 'y') {
        cout << "取消保存操作。\n";
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
        resetFrameBuffer();
        return false;
    }
    ofstream saveFile("2048_save.txt");
    if (!saveFile) {
        cout << "无法创建存档文件！\n";
#ifdef _WIN32
        Sleep(2000);
#else
        sleep(2);
#endif
        resetFrameBuffer();
        return false;
    }
    saveFile << score << "\n";
    for (auto& r : board) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            saveFile << r[j];
            if (j < BOARD_SIZE - 1) saveFile << " ";
        }
        saveFile << "\n";
    }
    saveFile.close();
    cout << "游戏已保存到 2048_save.txt\n";
#ifdef _WIN32
    Sleep(2000);
#else
    sleep(2);
#endif
    resetFrameBuffer();
    return true;
}

// 读取游戏
bool Game2048::loadGame() {
    clearScreen();
    cout << "\n══════════════════════════════════════════════════════\n";
    cout << "                   读取存档                          \n";
    cout << "══════════════════════════════════════════════════════\n\n";
    cout << "是否读取存档？当前游戏进度将丢失。(y/n): ";
    char confirm;
    cin >> confirm;
    cin.ignore();
    if (tolower(confirm) != 'y') {
        cout << "取消读取操作。\n";
#ifdef _WIN32
        Sleep(1000);
#else
        sleep(1);
#endif
        resetFrameBuffer();
        return false;
    }
    ifstream saveFile("2048_save.txt");
    if (!saveFile) {
        cout << "未找到存档文件！\n";
#ifdef _WIN32
        Sleep(2000);
#else
        sleep(2);
#endif
        resetFrameBuffer();
        return false;
    }
    int savedScore;
    saveFile >> savedScore;
    vector<vector<int>> savedBoard(BOARD_SIZE, vector<int>(BOARD_SIZE, 0));
    bool valid = true;
    for (int i = 0; i < BOARD_SIZE; i++) {
        for (int j = 0; j < BOARD_SIZE; j++) {
            if (!(saveFile >> savedBoard[i][j])) { valid = false; break; }
            if (savedBoard[i][j] != 0) {
                bool isPowerOfTwo = (savedBoard[i][j] & (savedBoard[i][j] - 1)) == 0;
                if (!isPowerOfTwo || savedBoard[i][j] < 0 || savedBoard[i][j] == 1) { valid = false; break; }
            }
        }
        if (!valid) break;
    }
    saveFile.close();
    if (!valid) {
        cout << "存档文件已损坏！\n";
#ifdef _WIN32
        Sleep(2000);
#else
        sleep(2);
#endif
        resetFrameBuffer();
        return false;
    }
    board = std::move(savedBoard);
    score = savedScore;
    haveWonFlag = false;
    practiceMode = false;
    practiceHistory.clear();
    practiceHistoryScores.clear();
    forcedSpawnNum = 0;
    forcedSpawnX = -1;
    forcedSpawnY = -1;
    spawnHint = "";
    prevBoard.assign(BOARD_SIZE, vector<int>(BOARD_SIZE, -1));
    prevScore = -1;

    cancelAIAnalysis();
    moveScores = vector<float>(4, 0.0f);
    aiBestMove = -1;

    resetFrameBuffer();
    cout << "游戏已从存档加载！\n";
    triggerAIAnalysis();
#ifdef _WIN32
    Sleep(2000);
#else
    sleep(2);
#endif
    return true;
}

// 游戏主循环
void Game2048::play() {
    bool gameOver = false;
    bool won = false;
    int lastTermW = termWidth;
    int lastTermH = termHeight;

    const int AI_MIN_DELAY = 100;
    const int AI_MAX_DELAY = 2000;

    triggerAIAnalysis();
    displayBoard();

    while (!gameOver) {
        updateTerminalSize();
        if (termWidth != lastTermW || termHeight != lastTermH) {
            resetFrameBuffer();
            displayBoard();
            lastTermW = termWidth;
            lastTermH = termHeight;
            continue;
        }

        if (openAI && aiEvaluating) {
            checkAIAnalysisResult();
            displayBoard();
        }

        // AI自动模式核心逻辑
        if (aiAutoMode) {
            if (keyboard.hasKeyPressed()) {
                char input = keyboard.getKey();
                switch (tolower(input)) {
                case '0':
                    aiAutoMode = false;
                    displayBoard();
                    continue;
                case ' ':
                    displayBoard();
                    continue;
                case 'q':
                    clearScreen();
                    cout << "\n游戏结束！最终分数: " << score << endl << flush;
                    return;
                default:
                    if (input == 'w' || input == 'a' || input == 's' || input == 'd' ||
                        input == '\033' || (input == '\340' || input == 0x00)) {
                        aiAutoMode = false;
                    }
                }
            }

            if (aiAutoMode && !aiEvaluating && aiBestMove >= 0) {
                bool validMove = false;
                switch (aiBestMove) {
                case 0: validMove = moveUp(); break;
                case 1: validMove = moveDown(); break;
                case 2: validMove = moveLeft(); break;
                case 3: validMove = moveRight(); break;
                default:
                    aiAutoMode = false;
                    moveCursor(termHeight - 2, 0);
                    cout << "\033[31mAI无有效移动，自动模式已关闭\033[0m" << flush;
                    displayBoard();
                    continue;
                }

                if (validMove) {
                    if (practiceMode) {
                        savePracticeState();
                    }
                    addRandomTile();

                    triggerAIAnalysis();
                    displayBoard();
                    prevBoard = board;
                    prevScore = score;

                    if (hasWon() && !won) {
                        won = true;
                        displayBoard();
                    }
                    if (!canMove()) {
                        gameOver = true;
                        aiAutoMode = false;
                    }
                }

#ifdef _WIN32
                Sleep(aiAutoMoveDelay);
#else
                usleep(aiAutoMoveDelay * 1000);
#endif
                continue;
            }
        }

        char input;
        if (keyboard.hasKeyPressed()) {
            input = keyboard.getKey();
        }
        else {
#ifdef _WIN32
            Sleep(10);
#else
            usleep(10000);
#endif
            continue;
        }

        bool validMove = false;

#ifdef _WIN32
        if (input == '\340' || input == 0x00) {
            input = keyboard.getKey();
            switch (input) {
            case 72: validMove = moveUp(); break;
            case 80: validMove = moveDown(); break;
            case 77: validMove = moveRight(); break;
            case 75: validMove = moveLeft(); break;
            default: continue;
            }
        }
#else
        if (input == '\033') {
            keyboard.getKey();
            input = keyboard.getKey();
            switch (input) {
            case 'A': validMove = moveUp(); break;
            case 'B': validMove = moveDown(); break;
            case 'C': validMove = moveRight(); break;
            case 'D': validMove = moveLeft(); break;
            default: continue;
            }
        }
#endif
        else {
            switch (tolower(input)) {
            case 'w': validMove = moveUp(); break;
            case 'a': validMove = moveLeft(); break;
            case 's': validMove = moveDown(); break;
            case 'd': validMove = moveRight(); break;
            case 'q':
                clearScreen();
                cout << "\n游戏结束！最终分数: " << score << endl << flush;
                return;
            case 'r':
                restartGame();
                displayBoard();
                continue;
            case 'm':
                keyboard.~KeyboardHandler();
                saveGame();
                new (&keyboard) KeyboardHandler();
                displayBoard();
                continue;
            case 'l':
                keyboard.~KeyboardHandler();
                loadGame();
                new (&keyboard) KeyboardHandler();
                displayBoard();
                continue;
            case 'h':
                keyboard.~KeyboardHandler();
                showhelp();
                keyboard.getKey();
                new (&keyboard) KeyboardHandler();
                displayBoard();
                continue;
            case 'u':
                resetFrameBuffer();
                displayBoard();
                continue;
            case 'p':
                keyboard.~KeyboardHandler();
                enterPracticeMode();
                new (&keyboard) KeyboardHandler();
                displayBoard();
                continue;
            case 'z':
                if (practiceMode) {
                    if (undoPractice()) {
                        displayBoard();
#ifdef _WIN32
                        Sleep(500);
#else
                        usleep(500000);
#endif
                    }
                    else {
#ifdef _WIN32
                        Sleep(500);
#else
                        usleep(500000);
#endif
                    }
                }
                continue;
            case 'k':
                if (practiceMode) {
                    handleForcedSpawnInput();
                    continue;
                }
                else {
                    moveCursor(termHeight - 1, 0);
                    cout << "\033[31m仅练习模式可使用此功能！\033[0m" << flush;
#ifdef _WIN32
                    Sleep(1000);
#else
                    usleep(1000000);
#endif
                    continue;
                }
            case 'i':
                openAI = !openAI;
                triggerAIAnalysis();
                displayBoard();
                continue;
            case '0':
                aiAutoMode = !aiAutoMode;
                if (aiAutoMode) {
                    openAI = true;
                    if (!aiEvaluating && aiBestMove < 0) {
                        startAsyncAIAnalysis();
                    }
                }
                displayBoard();
                continue;
            default: continue;
            }
        }

        if (validMove) {
            if (aiEvaluating) {
                cancelAIAnalysis();
            }

            if (practiceMode) {
                savePracticeState();
            }

            addRandomTile();
            triggerAIAnalysis();
            displayBoard();
            prevBoard = board;
            prevScore = score;

            if (hasWon() && !won) {
                won = true;
                displayBoard();
            }
            if (!canMove()) gameOver = true;
        }
    }

    moveCursor(termHeight, 0);
    cout << "\n══════════════════════════════════════════════════════\n";
    cout << "                   游戏结束！                         \n";
    cout << "                   最终分数: " << score << "          \n";
    cout << "                   最高分数: " << highScore << "      \n";
    if (won) cout << "              🎉 恭喜你获胜了！                    \n";
    else cout << "              没有可移动的方向了！                 \n";
    cout << "══════════════════════════════════════════════════════\n";
}

// 主函数
int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);
    SetConsoleCP(65001);
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE);
    DWORD modeOut = 0, modeIn = 0;
    GetConsoleMode(hOut, &modeOut);
    GetConsoleMode(hIn, &modeIn);
    SetConsoleMode(hOut, modeOut | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
    SetConsoleMode(hIn, modeIn & ~(ENABLE_QUICK_EDIT_MODE | ENABLE_INSERT_MODE));
    CONSOLE_CURSOR_INFO cursorInfo;
    GetConsoleCursorInfo(hOut, &cursorInfo);
    cursorInfo.bVisible = FALSE;
    SetConsoleCursorInfo(hOut, &cursorInfo);

    CONSOLE_FONT_INFOEX cfi;
    cfi.cbSize = sizeof(CONSOLE_FONT_INFOEX);
    cfi.nFont = 0;
    cfi.dwFontSize.X = 12;
    cfi.dwFontSize.Y = 12;
    cfi.FontFamily = FF_DONTCARE;
    cfi.FontWeight = FW_NORMAL;
    wcscpy(cfi.FaceName, L"Consolas");
    SetCurrentConsoleFontEx(hOut, FALSE, &cfi);
#endif

    srand(time(0));
    bool exitGame = false;
    while (!exitGame) {
        Game2048 game;
        game.play();

#ifdef _WIN32
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        CONSOLE_CURSOR_INFO cursorInfo;
        GetConsoleCursorInfo(hOut, &cursorInfo);
        cursorInfo.bVisible = TRUE;
        SetConsoleCursorInfo(hOut, &cursorInfo);
#endif

        cout << "\n是否重新开始游戏？(y/n): ";
        char playAgain;
        cin >> playAgain;
        cin.ignore();
        if (tolower(playAgain) != 'y') exitGame = true;
    }

    cout << "\033[0m\n感谢游玩！再见！\n" << flush;
    return 0;
}