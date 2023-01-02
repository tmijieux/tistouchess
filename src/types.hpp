#ifndef CHESS_TYPES_H
#define CHESS_TYPES_H


class chess_exception : public std::exception {
private:
    std::string m_what;
public:
    chess_exception(const std::string &what):
        m_what(what)
    {
    }

    const char* what() const noexcept override {
        return m_what.c_str();
    }
};

class invalid_fen_string : public std::exception {
    const char* what() const noexcept override {
        return "invalid fen string";
    }
};

enum Piece : uint8_t {
    P_EMPTY  = 0,
    P_PAWN   = 1,
    P_BISHOP = 2,
    P_KNIGHT = 3,
    P_ROOK   = 4,
    P_QUEEN  = 5,
    P_KING   = 6,
    P_INVALID_PIECE = 7,
    P_PIECE_MASK = 0x07,

    P_MIN_PIECE = 1,
    P_MAX_PIECE = 6,
    P_NUM_PIECE = 7,
};

enum Color : uint8_t {
    C_BLACK = 0x00,
    C_WHITE = 0x01,
    C_EMPTY = 0x02,
};
inline constexpr Color other_color(Color c) { return (Color)(0x01 - (uint8_t)c); }

enum CasleRightIndex: uint8_t {
    CR_KING_WHITE  = 1<<0,
    CR_QUEEN_WHITE = 1<<1,
    CR_KING_BLACK  = 1<<2,
    CR_QUEEN_BLACK = 1<<3,
};


struct Pos {
public:
    // signed row:6;
    // signed column:6;
    int8_t row;
    int8_t column;

    Pos(const Pos &p) :row{p.row}, column{ p.column }
    {
    }

    Pos(int8_t row_, int8_t col_) :row{ row_ }, column{ col_ }
    {
    }

    Pos(int8_t val) :row{ val>>3 }, column{val&7}
    {
    }

    Pos() : Pos(-1,-1) {}

    bool operator==(const Pos& o) const {
        return row == o.row && column == o.column;
    }
    const int8_t to_val() const { return row * 8 + column; }

    Pos& operator=(const Pos& o) {
        column = o.column;
        row = o.row;
        return *this;
    }

};


#endif // CHESS_TYPES_H
