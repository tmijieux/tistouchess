#include <iostream>
#include <functional>

#include <fmt/format.h>

#include "./move_ordering.hpp"
#include "./engine.hpp"
#include "./move_generation.hpp"
#include "./move.hpp"
#include "./timer.hpp"
#include "./evaluation.hpp"
#include "./uci.hpp"


int32_t NegamaxEngine::quiesce(
    Board& b, int color, int32_t alpha, int32_t beta, int current_depth)
{
    if (m_stop_required) {
        return beta; // fail-high immediately
    }

    int32_t standing_pat;
    //const auto &pos_string = b.get_pos_string();
    //auto it = m_evaluation.find(pos_string);
    // if (it != m_evaluation.end())
    // {
    //     standing_pat = it->second;
    // }
    // else
    {
        SmartTime st{ m_evaluation_timer };
        standing_pat = color * evaluate_board(b);
        //m_evaluation[pos_string] = standing_pat;
    }
    if (standing_pat + 4000 < alpha) {
        return alpha;
    }

    m_total_quiescence_nodes += 1;
    if (standing_pat >= beta) {
        return beta;
    }
    if (alpha < standing_pat) {
        alpha = standing_pat;
    }

    MoveList moveList;
    {
        SmartTime st{ m_move_generation2_timer };
        moveList = enumerate_moves(b, true);
    }
 /*   MoveList pv;
    KillerMoves kl;
    reorder_moves(b, moveList, 1000, 1000, pv, kl);*/
    reorder_mvv_lva(b, moveList);

    int num_legal_move = 0;
    for (auto& move : moveList) {
        {
            SmartTime st{ m_make_move2_timer };
            b.make_move(move);
        }
        if (b.is_king_checked(move.color))
        {
            {
                SmartTime st{ m_unmake_move2_timer };
                b.unmake_move(move);
            }
            move.legal_checked = true;
            move.legal = false;
            continue;
        }

        ++num_legal_move;
        move.legal_checked = true;
        move.legal = true;

        int32_t BIG_DELTA = 975;
        if (move.promote) {
            BIG_DELTA += 775;
        }
        int32_t pval = piece_value(move.taken_piece);
        if (pval + BIG_DELTA < alpha) {
            {
                SmartTime st{ m_unmake_move2_timer };
                b.unmake_move(move);
            }
            return alpha;
        }
        int32_t val = -quiesce(b, -color, -beta, -alpha, current_depth+1);
        {
            SmartTime st{ m_unmake_move2_timer };
            b.unmake_move(move);
        }

        if (val >= beta) {
            return beta;
        }
        if (val > alpha) {
            alpha = val;
        }
    }
    if (num_legal_move == 0) {
        Color clr = b.get_next_move();
        if (b.is_king_checked(clr)) {
            // deep trouble here (checkmate) , but the more moves to get there,
            // the less deep trouble because adversary could make a mistake ;)
            // (this is to select quickest forced mate)

            // should we return beta for fail-hard here ?
            // other wise aspiration windows may not work ???
            return -20000 + 5*current_depth;
        }
        else {
            // pat
            return 0;
        }
    }
    return alpha;
}

int32_t NegamaxEngine::negamax(
    Board& b,
    int max_depth, int remaining_depth, int current_depth,
    int color,
    int32_t alpha, int32_t beta,
    MoveList& parentPv,
    const MoveList& previousPv,
    MoveList* topLevelOrdering
) {
    if (m_stop_required) {
        return beta; // fail-high immediately
    }
    auto& stats = m_stats[max_depth][current_depth];


    Move bestMove;
    Move hash_move;
    bool has_hash_move = false;
    MoveList currentPv;
    
    Color clr = b.get_next_move();
    currentPv.reserve(remaining_depth+1);

    uint64_t mask = (1<<27) -1;
    uint64_t bkey = b.get_key();
    //uint32_t key = (uint32_t) (bkey & mask);
    auto &hashentry = m_hash.get(bkey);
    if (hashentry.key == bkey) {

        hash_move = generate_move_for_squares(
            b, hashentry.hashmove_src,
            hashentry.hashmove_dst,
            hashentry.promote_piece
        );
        has_hash_move = hash_move.src.row != -1;

        if (hashentry.depth >= remaining_depth) {
            //std::cout << "hash found"
            //    << " hashentry.depth=" << hashentry.depth
            //    << " remaining_depth=" << remaining_depth << "\n";
            if (hashentry.exact_score) {
                //std::cout << "hash hit!\n";
                stats.num_hash_hits++;

                // collect pv
                if (parentPv.size() == 0) {
                    parentPv.push_back(hash_move);
                }
                else {
                    parentPv[0] = hash_move;
                }
                parentPv.resize(1);
                parentPv.insert(parentPv.begin() + 1, currentPv.begin(), currentPv.end());

                if (hashentry.score >= beta) {
                    return beta;
                }
                else if (hashentry.score <= alpha) {
                    return alpha;
                }
                else {
                    return hashentry.score;
                }
            }
            else if (hashentry.lower_bound) {
                if (hashentry.score >= beta) {
                    //std::cout << "hash hit!\n";
                    stats.num_hash_hits++;

                    return beta;
                }
                else if (hashentry.score >= alpha) {
                    alpha = hashentry.score;
                }
            }
            else if (hashentry.upper_bound) {
                if (hashentry.score <= alpha) {
                    stats.num_hash_hits++;

                    //std::cout << "hash hit!\n";
                    return alpha;
                }
            }
        }
    } else if (hashentry.key != 0 && hashentry.key != bkey) {
        stats.num_hash_conflicts++;
    }

    if (remaining_depth == 0) {
        stats.num_leaf_nodes += 1;
        SmartTime st{ m_quiescence_timer };
        int32_t val = quiesce(b, color, alpha, beta, current_depth);
        return val ;
    }
    m_total_nodes += 1;

    MoveList moveList;
    if (topLevelOrdering !=  nullptr && topLevelOrdering->size() > 0) {
        moveList = *topLevelOrdering;
    } else {
        {
            SmartTime st{ m_move_generation_timer };
            moveList = enumerate_moves(b);
        }
        {
            SmartTime st{ m_move_ordering_timer };
            reorder_moves(b, moveList, current_depth,
                remaining_depth, previousPv, m_killers,
                hash_move, has_hash_move
            );
        }
    }
    uint32_t num_legal_move = 0;

    bool cutoff = false;
    bool raise_alpha = false;
    int num_move_visited = 0;
    bool use_aspiration = false;
    int32_t oldval = -999999;

    for (auto& move : moveList) {
        if (move.legal_checked && !move.legal) {
            continue;
        }
        ++num_move_visited;
        {
            SmartTime st{ m_make_move_timer };
            b.make_move(move);
        }

        if (b.is_king_checked(clr)) {
            // illegal move
            move.legal_checked = true;
            move.legal = false;
            move.evaluation = std::numeric_limits<int32_t>::min();
            {
                SmartTime st{ m_unmake_move_timer };
                b.unmake_move(move);
            }
            continue;
        }
        move.legal_checked = true;
        move.legal = true;
        ++num_legal_move;
        int32_t val = 0;
        if (use_aspiration && remaining_depth >= 2) {
            val = -negamax(
                b, max_depth, remaining_depth - 1, current_depth + 1,
                -color, -alpha-1, -alpha,
                currentPv, previousPv, nullptr
            );

            if (val > alpha && val < beta) {
                int32_t lower = -alpha - 1;
                int32_t initial_window_size = beta - alpha;

                int k = 0;
                while (val > alpha && val < beta && lower > -beta && k <= 3) {
                    // while search window failed high we increment window
                    double coeff = 1.0 / (8 >> k);
                    if (k < 3) {
                        lower = std::min(-alpha - (int32_t)(coeff * initial_window_size), lower-1);
                    }
                    else {
                        lower = -beta;
                    }
                    // 1/8 , 1/4, 1/2, and eventually 1/1
                    val = -negamax(
                        b, max_depth, remaining_depth - 1, current_depth + 1,
                        -color, lower, -alpha,
                        currentPv, previousPv, nullptr
                    );
                    ++k;
                }
            }
        }
        else {
            val = -negamax(
                b, max_depth, remaining_depth - 1, current_depth + 1,
                -color, -beta, -alpha,
                currentPv, previousPv, nullptr
            );
        }
        {
            SmartTime st{ m_unmake_move_timer };
            b.unmake_move(move);
        }
        if (val > oldval) {
            oldval = val;
            bestMove = move;
        }
        if (val >= beta) {
            alpha = beta; // cut node ! yay !
            if (m_stop_required) {
                return beta;
            }
            bestMove = move;
            cutoff = true;
            if (move.best_from_pv) {
                stats.num_cut_by_best_pv += 1;
            }
            else if (move.killer) {
                stats.num_cut_by_killer += 1;
            }
            if (!move.takes && !move.killer) {
                move.killer = true;
                move.mate_killer = val >= 20000 - (max_depth+1) * 5;
                bool already_in = false;
                auto &killers = m_killers[current_depth];
                //replace killer
                for (const auto &m : killers) {
                    if (m == move) {
                        already_in = true;
                        break;
                    }
                }

                if (!already_in) {
                    killers.push_back(move);
                    if (killers.size() > 10) {
                        // pop front
                        killers.erase(killers.begin());
                    }
                }
            }
            // TODO: store refutation move in TT
            break;
        }
        if (val > alpha) {
            alpha = val; // pv node
            bestMove = move;

            // collect PV :
            if (parentPv.size() == 0) {
                parentPv.push_back(move);
            }
            else {
                parentPv[0] = move;
            }
            parentPv.resize(1);
            parentPv.insert(parentPv.begin() + 1, currentPv.begin(), currentPv.end());
            raise_alpha = true;
            // std::cerr << "alpha increase\n";
            use_aspiration = true;
        }
    }

    stats.num_move_visited += num_move_visited;
    stats.num_move_skipped += moveList.size() - num_move_visited;
    stats.num_move_generated += moveList.size();
    stats.num_nodes += 1;

    bool replace = hashentry.key == 0 || remaining_depth > hashentry.depth;
    if (replace) {
        hashentry.score = alpha;
        hashentry.depth = remaining_depth;
        hashentry.key = bkey;
        hashentry.hashmove_src = bestMove.src.to_val();
        hashentry.hashmove_dst = bestMove.dst.to_val();
        hashentry.promote_piece = bestMove.promote_piece;
    }

    if (cutoff) {
        // cut-node (fail-high)
        // "A fail-high indicates that the search found something that was "too good".
        // What this means is that the opponent has some way, already found by the search,
        // of avoiding this position, so you have to assume that they'll do this.
        // If they can avoid this position, there is no longer any need to search successors,
        // since this position won't happen."
        stats.num_cutoffs += 1;
        if (replace){
            hashentry.exact_score = false;
            hashentry.lower_bound = true;
            hashentry.upper_bound = false;
        }
    } else if (raise_alpha) {
        // pv-node (new best move)
        stats.num_pvnode += 1;
        if (replace){
            hashentry.exact_score = true;
            hashentry.lower_bound = false;
            hashentry.upper_bound = false;
        }
    } else {
        // all-node (fail-low)
        // "A fail-low indicates that this position was not good enough for us.
        // We will not reach this position,
        // because we have some other means of reaching a position that is better.
        // We will not make the move that allowed the opponent to put us in this position."
        stats.num_faillow_node += 1;
        if (replace){
            hashentry.exact_score = false;
            hashentry.lower_bound = false;
            hashentry.upper_bound = true;
        }
    }

    if (num_legal_move == 0) {
        if (b.is_king_checked(clr)) {
            // deep trouble here (checkmate) , but the more moves to get there,
            // the less deep trouble because adversary could make a mistake ;)
            // (this is to select quickest forced mate)
            return -20000 + 5*current_depth;
        }
        else {
            // pat
            return 0;
        }
    }
    if (current_depth == 0) {
        std::sort(moveList.begin(), moveList.end(), [](auto& a, auto& b) {
            return a.evaluation > b.evaluation;
        });
        *topLevelOrdering = moveList;
        // full sort for next level of iterative deepening
    }
    return alpha;
}

void NegamaxEngine::_start_uci_background(Board &b)
{
    bool move_found = false;
    Move best_move;
    GoParams p = m_uci_go_params;
    if (p.depth == 0) {
        p.depth = 7;
    }
    uint64_t time = 0;
    if (p.movetime > 0) {
        time = p.movetime;
    }
    else if (p.wtime > 0 || p.btime > 0) {
        uint64_t basetime = b.get_next_move() == C_WHITE ? p.wtime : p.btime;
        if (p.movestogo > 0) {
            time = basetime / p.movestogo;
        }
        else {
            int move_count_wanted = std::max(60u - b.get_full_move(), 10u);
            time = basetime / move_count_wanted;
        }
        // remove 200ms to have time finishing and returning move
        time = std::min(time, std::max(15ull, basetime - 200ull));
    }
    auto id = ++m_run_id;

    if (time > 0) {
         // schedule thread to interrupt
        std::thread([this,time, id]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(time));
            if (this->is_running() && this->m_run_id == id)
            {
                this->m_stop_required_by_timeout = true;
                this->m_stop_required = true;
            }
        })
            .detach();
    }

    m_running = true;
    bool interrupted = iterative_deepening(
        b, p.depth, &best_move, &move_found, time
    );
    m_running = false;
    bool is_timeout = false;

    if (move_found) {
        uci_send_bestmove(best_move);
        move_found = false;
    }
    if (!interrupted || m_stop_required_by_timeout) {
        m_thread.detach();
    }
    m_stop_required = false;
    m_stop_required_by_timeout = false;
}

void NegamaxEngine::start_uci_background(Board &b)
{
    if (m_running) {
        throw std::exception("engine already running");
    }
    m_thread = std::thread{
        std::bind( &NegamaxEngine::_start_uci_background,this, b)
    };
}

void NegamaxEngine::stop()
{
    if (m_thread.joinable())
    {
        m_stop_required = true;
        m_thread.join();
    }
    m_stop_required = false;
    m_running = false;
}

bool NegamaxEngine::iterative_deepening(
    Board& b, int max_depth,
    Move *bestMove, bool *moveFound,
    uint64_t max_time_ms)
{
    *moveFound = false;

    int color = b.get_next_move() == C_WHITE ? +1 : -1;
    MoveList previousPv;
    MoveList topLevelOrdering;

    this->set_max_depth(max_depth);
    Timer total_timer;
    total_timer.start();

    for (int depth = 1; depth <= max_depth; ++depth) {
        MoveList newPv;
        newPv.reserve(depth);
        Timer t;
        t.reset();
        t.start();

        reset_timers();
        m_total_nodes = 0;
        m_total_quiescence_nodes = 0;

        int32_t score = this->negamax(
            b, depth, depth, 0, color,
            -999999, // alpha
            +999999, // beta
            newPv, previousPv, &topLevelOrdering
        );
        //std::cerr << "\n\n-----------------\n";
        if (m_stop_required_by_timeout || max_time_ms > 0) {
            uint64_t total_duration = (uint64_t)(total_timer.get_micro_length() / 1000.0);
            if (total_duration > max_time_ms) {
                uci_send_info_string(
                    "EXIT ON TIME total_duration={} max_time_ms={} move_found={}",
                    total_duration, max_time_ms, *moveFound
                );
                return false;
            }
        }
        if (m_stop_required) {
            return true;
        }
        if (newPv.size() > 0) {
            //std::cerr << "Best move for depth="
            //    << depth << "  = " << move_to_string(newPv[0])
            //    << "\n";
        }
        else {
            *moveFound = false;
            return false;
        }

        t.stop();

        std::vector<std::string> moves_str;
        moves_str.reserve(newPv.size());
        for (const auto& m : newPv) {
            moves_str.emplace_back(move_to_string(m));
        }
        uci_send("info string PV = {}\n", fmt::join(moves_str, " "));


        if (depth >= 0) {

            moves_str.clear();
            moves_str.reserve(newPv.size());
            for (const auto& m : newPv) {
                moves_str.emplace_back(move_to_uci_string(m));
            }
            uint64_t total_nodes = m_total_nodes + m_total_quiescence_nodes;
            double duration = std::max(t.get_length(), 0.001); // cap at 1ms
            uint64_t nps = (uint64_t)(total_nodes / duration);
            uint64_t time = (uint64_t)(t.get_micro_length() / 1000);

            uci_send(
                "info depth {} score cp {} nodes {} nps {} pv {} time {}\n",
                depth, score, total_nodes, nps, fmt::join(moves_str, " "), time
            );
        }

        *bestMove = newPv[0];
        *moveFound = true;

        previousPv = std::move(newPv);
        this->display_stats(depth);

        std::cerr << "duration="<<t.get_length()<<"\n";
        display_timers();
        std::cerr << "total_nodes="<<m_total_nodes<<"\n";
        std::cerr << "total_quiescence_nodes="<<m_total_quiescence_nodes<<"\n";
        std::cerr << "\n-----------------\n\n";
    }
    return false;
}

std::pair<bool,Move> find_best_move(Board& b)
{
    Move move;
    bool found;
    NegamaxEngine engine;

    engine.iterative_deepening(b, 7, &move, &found, 0);
    if (!found) {
        std::cerr << "WARNING no move found !!" << std::endl;
    }
    return std::make_pair(found,move);
}

uint64_t NegamaxEngine::perft(
    Board &b,
    int max_depth, int remaining_depth,
    std::vector<uint64_t>& res)
{
    if (remaining_depth == 0) {
        return 1;
    }

    MoveList ml = enumerate_moves(b);
    Color clr = b.get_next_move();

    uint64_t total = 0;
    int num_legal_move = 0;
    for (auto& move : ml) {
        b.make_move(move);
        if (b.is_king_checked(clr)) {
            b.unmake_move(move);
            continue;
        }
        ++num_legal_move;
        uint64_t val = perft(b, max_depth, remaining_depth-1, res);

        total += val;
        b.unmake_move(move);
        if (max_depth == remaining_depth) {
            std::cout << move_to_uci_string(move) <<": "<<val<< " "<<move_to_string(move) << "\n";
        }
    }
    res[max_depth-remaining_depth] += num_legal_move;
    return total;
}

void NegamaxEngine::do_perft(Board &b, int depth)
{
    std::vector<uint64_t> res;
    res.resize(depth);
    std::fill(res.begin(), res.end(), 0);

    perft(b, depth, depth, res);

    for (int i = 0; i < depth; ++i) {
        uci_send_info_string(
            "num_move for depth {} = {}",
            i, res[i]
        );
    }
}
