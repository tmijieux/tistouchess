#include <vector>
#include <iostream>

#include "./fen_reader.hpp"
#include "./move_generation.hpp"
#include "./board.hpp"
#include "./transposition_table.hpp"
#include "./uci.hpp"

void Board::load_position(const std::string& fen_position)
{
    FenReader r;
    r.load_position(*this, fen_position);
    m_key = HashMethods::full_hash(*this);
}

void load_test_position(Board &b, int position)
{
    switch (position) {
    default:
    case 1: b.load_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1"); break;
    case 2: b.load_position("r3k2r/p1ppqpb1/bn2pnp1/3PN3/1p2P3/2N2Q1p/PPPBBPPP/R3K2R w KQkq - 0 1"); break;
    case 3: b.load_position("8/2p5/3p4/KP5r/1R3p1k/8/4P1P1/8 w - -"); break;
    case 4: b.load_position("r3k2r/Pppp1ppp/1b3nbN/nP6/BBP1P3/q4N2/Pp1P2PP/R2Q1RK1 w kq - 0 1"); break;
    case 5: b.load_position("rnbq1k1r/pp1Pbppp/2p5/8/2B5/8/PPP1NnPP/RNBQK2R w KQ - 1 8"); break;
    case 6: b.load_position("r4rk1/1pp1qppp/p1np1n2/2b1p1B1/2B1P1b1/P1NP1N2/1PP1QPPP/R4RK1 w - - 0 10"); break;
    case 7: b.load_position("8/7R/2p1k3/p3P2P/1p6/1P1r4/1KP4r/8 b - - 0 1"); break; // mate in 7, some quiet moves
    case 8: b.load_position("r4rk1/ppq2Np1/1n1pb3/2p4p/8/3B2Q1/PPPB2PP/5RK1 w - - 0 1"); break; // mate in 3 , queen sac first, all checks
    }

}

void Board::load_initial_position()
{
    load_position("rnbqkbnr/pppppppp/8/8/8/8/PPPPPPPP/RNBQKBNR w KQkq - 0 1");
    //load_position("8/7R/2p1k3/p3P2P/1p6/1P1r4/1KP4r/8 b - - 0 1");
}

Piece Board::get_piece_at(const Pos& pos) const {
    if (pos.column > 7 || pos.row > 7) {
        std::abort();
    }
    auto x = pos.column + pos.row * 8;
    uint8_t q = x >> 1;
    uint8_t n = x & 1;
    uint8_t val = (m_board[q] >> (4 * n)) & P_PIECE_MASK;
    return (Piece)val;
}

Color Board::get_color_at(const Pos& pos) const {

    if (pos.column > 7 || pos.row > 7) {
        std::abort();
    }
    uint8_t x = pos.column + pos.row * 8;
    uint8_t q = x >> 1;
    uint8_t n = x & 1;
    uint8_t val = (m_board[q] >> (4 * n)) & 0x0F;
    if (val == 0) {
        return C_EMPTY;
    }
    return (Color) (val >> 3);
}

void Board::set_piece_at(const Pos& pos, Piece p, Color c) {
#ifdef CHESS_DEBUG
    if (pos.column > 7 || pos.row > 7) {
        std::abort();
    }
    if (c == C_WHITE && p == P_EMPTY) {
        std::abort();
    }
    if (p == P_INVALID_PIECE) {
        std::abort();
    }
#endif
    uint8_t x = pos.column + pos.row * 8;
    uint8_t q = x >> 1;
    uint8_t n = x & 1;

    uint8_t store = m_board[q];
    uint8_t newval = ((uint8_t)p) | (((uint8_t)c)<<3);


    m_board[q] = (store & (0x0F << (4 * (1-n))))  | (newval << (4 * n));

    if (p == P_KING) {
        set_king_pos(pos, c);
    }
}

bool Board::is_square_attacked(const Pos &pos, Color attacked_by_clr)
{
    MoveList ml;
    bool fake_piece = get_piece_at(pos) == P_EMPTY;
    if (fake_piece) {
        set_piece_at(pos, P_PAWN, other_color(attacked_by_clr));
    }
    find_move_to_position(*this, pos, ml, attacked_by_clr, 1, true);
    if (fake_piece) {
        set_piece_at(pos, P_EMPTY, C_BLACK);
    }
    return ml.size() == 1;
}

int8_t Board::compute_king_checked(Color clr)
{ // start from king position and do 'inversed-move' of all type of piece
  // to see if we land on a threatening piece
    Pos p = get_king_pos(clr);
    MoveList ml;
    find_move_to_position(*this, p, ml, other_color(clr), 1, true);
    return ml.size() == 1 ? 1 : 0;
}

void Board::make_move(const Move& move)
{
    //check_valid_state();

    #ifdef CHESS_DEBUG
    if (move.color != get_next_move()) {
        throw std::exception("invalid move for next_to_move state");
    }
    #endif // DEBUG

    // ----------------------------------------
    // --- HANDLE REMOVING CASTLING RIGHTS ----
    bool isWhite = move.color == C_WHITE;
    const uint8_t old_castle_rights = get_castle_rights();
    uint8_t new_castle_rights = old_castle_rights;

    // by moving
    if (move.piece == P_ROOK) {
        if (move.src.column == 0) {
            new_castle_rights &= ~(isWhite ? CR_QUEEN_WHITE : CR_QUEEN_BLACK);
        }
        else if (move.src.column == 7) {
            new_castle_rights &= ~(isWhite ? CR_KING_WHITE : CR_KING_BLACK);
        }
    }
    else if (move.piece == P_KING) {
        new_castle_rights &= ~(
            isWhite
            ? (CR_KING_WHITE|CR_QUEEN_WHITE)
            :(CR_KING_BLACK|CR_QUEEN_BLACK)
        );
    }
    // by capturing
    if (move.takes && move.taken_piece == P_ROOK) {
        if (move.dst.column == 7) {
            new_castle_rights &= ~(isWhite ? CR_KING_BLACK : CR_KING_WHITE);
        } else if (move.dst.column == 0) {
            new_castle_rights &= ~(isWhite ? CR_QUEEN_BLACK : CR_QUEEN_WHITE);
        }
    }
    if (new_castle_rights != old_castle_rights) {
        set_castle_rights(new_castle_rights);
    }

    // ------------------------
    // --- UPDATE HASH KEY ----
    HashMethods::make_move(*this, m_key, move, old_castle_rights ^ new_castle_rights);

    // -------------------------------
    // ------ MOVE PIECES AROUND -----
    if (move.promote) {
        set_piece_at(move.dst, move.promote_piece, move.color);
    }
    else {
        set_piece_at(move.dst, move.piece, move.color);
    }
    set_piece_at(move.src, P_EMPTY, C_BLACK);
    if (move.en_passant) {
        set_piece_at(get_en_passant_pos(), P_EMPTY, C_BLACK);
    }
    if (move.castling) {
        uint8_t dstCol = move.dst.column;
        uint8_t row = move.src.row;
        Pos rook_src{ row, u8(dstCol == 6 ? 7 : 0) };
        Pos rook_dst{ row, u8(dstCol == 6 ? 5 : 3) };

        // update rook position
        set_piece_at(rook_src, P_EMPTY, C_BLACK);
        set_piece_at(rook_dst, P_ROOK, move.color);
    }

    // -------------------------------
    // ------ EN PASSANT STATE -------
    if (move.piece == P_PAWN && std::abs(move.src.row - move.dst.row) == 2) {
        set_en_passant_pos(move.dst.column | CAN_EN_PASSANT);
    } else {
        set_en_passant_pos(0);
    }
    // -------------------------------
    // -------- SIDE TO MOVE  --------
    set_next_move(other_color(get_next_move()));


    // -------------------------------
    // ----- 50 move rule clock  -----
    if (move.takes || move.piece == P_PAWN) {
        m_half_move_counter = 0;
    }
    else {
        ++m_half_move_counter;
    }

    // ------------------------------------
    // ----- PLY COUNTER (not needed because we have depth)??
    ++m_ply_count;

    // ------------------------------------
    // ----- CHECK STATE (can be computed)
    uint8_t checks = (
        compute_king_checked(C_BLACK)
        | (compute_king_checked(C_WHITE) << 1u)
    );
    set_king_checked(checks);

    //check_valid_state();

}

void Board::unmake_move(const Move& move)
{
    //check_valid_state();

    // -----------------------------------
    // ------ ALL IRREVERSIBLE STATE -----
    // (EN PASSANT/50-M-CLOCK/CASTLING-RIGHTS)
    m_flags = move.m_board_state_before;
    m_key = move.m_board_key_before;
    std::string before_unmake = write_fen_position(*this);

    // -------------------------------
    // ------ MOVE PIECES AROUND -----
    set_piece_at(move.src, move.piece, move.color);
    Color clr = move.takes ? other_color(move.color) : C_BLACK;
    if (move.en_passant) {
        set_piece_at(get_en_passant_pos(), P_PAWN, clr);
        set_piece_at(move.dst, P_EMPTY, C_BLACK);
    } else {
        set_piece_at(move.dst, move.taken_piece, clr);
    }
    if (move.castling) {
        uint8_t dstCol = move.dst.column;
        uint8_t row = move.src.row;
        Pos rook_src{ row, u8(dstCol == 6 ? 7 : 0) };
        Pos rook_dst{ row, u8(dstCol == 6 ? 5 : 3) };
        set_piece_at(rook_src, P_ROOK, move.color);
        set_piece_at(rook_dst, P_EMPTY, C_BLACK);
    }
    --m_ply_count;
    m_half_move_counter = move.half_move_before;

 /*   if (!check_valid_state()) {
        Board b;
        std::cerr << "before_unmake=" << before_unmake << "\n";
        b.load_position(before_unmake);
        console_draw(b);

        std::cerr << "after_unmake=" << this->get_fen_string() << "\n";
        console_draw(*this);
        std::cerr << "here\n";
    }*/
}


void Board::make_null_move(NullMove& m)
{
    //check_valid_state();

    ++m_ply_count;

    // ------------------------
    // --- UPDATE HASH KEY ----
    HashMethods::make_null_move(*this, m_key, m);

    // -------------------------------
    // -------- SIDE TO MOVE  --------
    set_next_move(other_color(get_next_move()));

    // -------------------------------
    // ------ EN PASSANT STATE -------
    if (has_en_passant()) {
        set_en_passant_pos(0);
    }

    //check_valid_state();

}

void Board::unmake_null_move(NullMove& m)
{
    //check_valid_state();

    --m_ply_count;
    set_next_move(other_color(get_next_move()));
    m_key = m.m_board_key_before;
    m_flags = m.m_board_state_before;
    m_half_move_counter = m.half_move_before;

    //check_valid_state();
}

bool Board::check_valid_state()
{
    uint8_t count_piece[6 * 2] = { 0,0 };
    for (uint8_t i = 0; i < 64; ++i)
    {
        Color clr = get_color_at(i);
        Piece piece = get_piece_at(i);

        if (clr != C_EMPTY)
        {
            ++count_piece[clr * 6 + (piece-1)];
        }
    }
    for (uint8_t c = 0; c < 2; ++c)
    {
        if (count_piece[c * 6 + P_PAWN-1] > 8) {
            return false;
        }
        if (count_piece[c * 6 + P_BISHOP-1] > 10) {
            return false;
        }
        if (count_piece[c * 6 + P_KNIGHT-1] > 10) {
            return false;
        }
        if (count_piece[c * 6 + P_ROOK-1] > 10) {
            return false;
        }
        if (count_piece[c * 6 + P_QUEEN-1] > 9) {
            return false;
        }
        if (count_piece[c * 6 + P_KING-1] > 1) {
            return false;
        }
    }
    return true;
}