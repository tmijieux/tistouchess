
#include <cstring>
#include <istream>
#include <sstream>

#include "./types.hpp"
#include "./fen_reader.hpp"
#include "./board.hpp"
#include "./move_generation.hpp"

inline constexpr Color get_color_by_char_fen(char c)
{
    if ('a' <= c && c <= 'z') {
        return C_BLACK;
    }
    else if ('A' <= c && c <= 'Z') {
        return C_WHITE;
    }
    else {
        return C_EMPTY;
    }
}

void remove_whitespace(std::istream& input)
{
    char c;
    do {
        c = input.get();
    } while (c == ' ');
    input.putback(c);
}

void fen_read_position(Board &b, std::istream& input)
{
    Pos cur_pos{ 7, 0 };
    for (;;) {
        char c = input.get();
        if (c == ' ') {

            break;
        }
        if (c == '/') {
            if (cur_pos.column != 0 || cur_pos.row >= 8) {
                throw invalid_fen_string();
            }
            continue;
        }
        if (cur_pos.row > 7) {
            break;
        }

        Piece p = get_piece_by_char_fen(c);
        Color clr = get_color_by_char_fen(c);

        if (p != P_INVALID_PIECE)
        {
            b.set_piece_at(cur_pos, p, clr);
            ++cur_pos.column;
            if (cur_pos.column == 8) {
                cur_pos.column = 0;
                --cur_pos.row;
            }
        }
        else if ('1' <= c && c <= '8')
        {
            int length = c - '0';
            for (int i = 0; i < length; ++i) {
                b.set_piece_at(cur_pos, P_EMPTY, C_BLACK);
                ++cur_pos.column;
                if (cur_pos.column == 8) {
                    cur_pos.column = 0;
                    --cur_pos.row;
                }
            }
        }
        else {
            throw invalid_fen_string();
        }
    }
    remove_whitespace(input);
}

void fen_read_next_to_move(Board& b, std::istream& input)
{
    for (;;) {
        char c = input.get();
        if (c == ' ') {
            break;
        }
        else if (c == 'w') {
            b.set_next_move(C_WHITE);
            break;
        }
        else if (c == 'b') {
            b.set_next_move(C_BLACK);
            break;
        }
        else {
            throw invalid_fen_string();
        }
    }
    remove_whitespace(input);
}

void fen_read_castle_rights(Board& b, std::istream& input)
{
    uint8_t castle_rights = 0x00;

    for (;;) {
        char c = input.get();
        if (c == ' ' || c == '-') {
            break;
        }
        else if (c == 'k') {
            castle_rights |= CR_KING_BLACK;
        }
        else if (c == 'q') {
            castle_rights |= CR_QUEEN_BLACK;
        }
        else if (c == 'K') {
            castle_rights |= CR_KING_WHITE;
        }
        else if (c == 'Q') {
            castle_rights |= CR_QUEEN_WHITE;
        }
        else {
            throw invalid_fen_string();
        }
    }
    b.set_castle_rights(castle_rights);
    remove_whitespace(input);
}

void fen_read_en_passant_position(Board& b, std::istream& input)
{
    Pos en_passant_pos{ 0 };
    for (;;) {
        char c = input.get();
        if (c == '-') {
            en_passant_pos = Pos{ 0 };
            break;
        }
        else if (c == ' ') {
            break;
        }
        if ('a' <= c && c <= 'h') {
            en_passant_pos.column = c - 'a';
        }
        else if ('1' <= c && c <= '8') {
            en_passant_pos.row = c - '1';
        }
        else {
            throw invalid_fen_string();
        }
    }

    if (en_passant_pos.row == 0)
        b.set_en_passant_pos(0);
    else
        b.set_en_passant_pos(CAN_EN_PASSANT | en_passant_pos.column);
    remove_whitespace(input);
}


void FenReader::load_position(Board &b, const std::string&fen_position) const
{
    std::istringstream reader(fen_position);
    remove_whitespace(reader);

    fen_read_position(b, reader);
    fen_read_next_to_move(b, reader);
    fen_read_castle_rights(b, reader);
    fen_read_en_passant_position(b, reader);

    int half_move = 0;
    reader >> half_move;
    b.set_half_move(half_move);

    int full_move = 1;
    reader >> full_move;
    Color clr = b.get_next_move();
    if (clr == C_WHITE) {
        b.set_ply_count(2*full_move-2);
    }
    else {
        b.set_ply_count(2*full_move - 1);
    }
}


std::string write_fen_position(const Board& b)
{
    constexpr int len = 8 * 8 + 7 + 40;
    char buf[len] = "";
    std::memset(buf, 0, sizeof buf);

    int x = 0;
    for (uint8_t irow = 0; irow < 8; ++irow) {
        uint8_t row = 7 - irow;
        int num_empty = 0;
        for (uint8_t col = 0; col < 8; ++col) {
            auto pos = Pos{ row, col };
            Piece p = b.get_piece_at(pos);
            if (p != P_EMPTY) {
                if (num_empty > 0) {
                    char c = '0' + num_empty;
                    buf[x++] = c;
                    num_empty = 0;
                }
                char c = get_fen_char_by_piece(p);
                if (b.get_color_at(pos) == C_WHITE) {
                    c += 'A' - 'a';
                }
                buf[x++] = c;
            }
            else {
                ++num_empty;
            }
        }
        if (num_empty > 0) {
            char c = '0' + num_empty;
            buf[x++] = c;
        }
        if (row > 0) {
            buf[x++] = '/';
        }
    }
    buf[x++] = ' ';
    buf[x++] = b.get_next_move() == C_WHITE ? 'w' : 'b';
    buf[x++] = ' ';

    char castlingChars[4] = { 'K','Q','k','q' };
    bool hasCastling = false;
    auto rights = b.get_castle_rights();
    for (int i = 0; i < 4; ++i) {
        if (rights & (1<<i)) {
            buf[x++] = castlingChars[i];
            hasCastling = true;
        }
    }
    if (!hasCastling) {
        buf[x++] = '-';
    }
    buf[x++] = ' ';
    Pos eppos = b.get_en_passant_pos();
    if (eppos.row > 0) {
        std::string v = pos_to_square_name(eppos);
        for (size_t i = 0; i < v.size(); ++i) {
            buf[x++] = v[i];
        }
    }
    else {
        buf[x++] = '-';
    }
    buf[x++] = ' ';
    buf[x] = 0;
    auto val = std::string(buf);
    return val + std::to_string((int)b.get_half_move())+" "+std::to_string(b.get_full_move());
}
