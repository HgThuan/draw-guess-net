#include "scoring.h"
#include <stdint.h>

int scoring_guess(int rank_k, int remaining_s, int draw_time_s)
{
    if (rank_k <= 0 || draw_time_s <= 0 || remaining_s < 0 || remaining_s > draw_time_s) {
        return 0;
    }

    int64_t time_bonus = (400LL * (int64_t)remaining_s) / (int64_t)draw_time_s;
    int64_t rank_penalty = 25LL * (int64_t)(rank_k - 1);
    int64_t points = 100LL + time_bonus - rank_penalty;

    if (points < 50) {
        return 50;
    }
    return (int)points;
}

int scoring_drawer(int total_guess_points, int player_count)
{
    if (total_guess_points <= 0 || player_count <= 1) {
        return 0;
    }
    return total_guess_points / (player_count - 1);
}
