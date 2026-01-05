#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_LINE 8192
#define MAX_ITERATION_BOUND 1000000

typedef struct {
    int n;
    int len;
    long long expanded;
} SearchContext;

static void print_state(const int *state, int n) {
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            if (c > 0) {
                printf(",");
            }
            printf("%d", state[r * n + c]);
        }
        printf("\n");
    }
}

static int count_misplaced(const int *state, int len) {
    int misplaced = 0;
    for (int i = 0; i < len; i++) {
        if (state[i] == -1) {
            if (i != len - 1) {
                misplaced++;
            }
            continue;
        }
        if (i == len - 1) {
            misplaced++;
            continue;
        }
        if (state[i] != i) {
            misplaced++;
        }
    }
    return misplaced;
}

static int manhattan_distance(const int *state, int n) {
    int len = n * n;
    int distance = 0;
    for (int idx = 0; idx < len; idx++) {
        int value = state[idx];
        if (value == -1) {
            continue;
        }
        int goal_row = value / n;
        int goal_col = value % n;
        int cur_row = idx / n;
        int cur_col = idx % n;
        distance += abs(goal_row - cur_row) + abs(goal_col - cur_col);
    }
    return distance;
}

static bool is_goal(const int *state, int len) {
    for (int i = 0; i < len - 1; i++) {
        if (state[i] != i) {
            return false;
        }
    }
    return state[len - 1] == -1;
}

static char opposite_move(char move) {
    switch (move) {
        case 'U':
            return 'D';
        case 'D':
            return 'U';
        case 'L':
            return 'R';
        case 'R':
            return 'L';
        default:
            return '\0';
    }
}

static bool apply_move(int *state, int n, int *blank_index, char move) {
    int row = *blank_index / n;
    int col = *blank_index % n;
    int new_row = row;
    int new_col = col;

    switch (move) {
        case 'U':
            new_row--;
            break;
        case 'D':
            new_row++;
            break;
        case 'L':
            new_col--;
            break;
        case 'R':
            new_col++;
            break;
        default:
            return false;
    }

    if (new_row < 0 || new_row >= n || new_col < 0 || new_col >= n) {
        return false;
    }

    int new_index = new_row * n + new_col;
    int temp = state[new_index];
    state[new_index] = -1;
    state[*blank_index] = temp;
    *blank_index = new_index;
    return true;
}

static bool read_ini(const char *path, int **out_state, int *out_n, int *out_blank) {
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Failed to open %s: %s\n", path, strerror(errno));
        return false;
    }

    char line[MAX_LINE];
    if (!fgets(line, sizeof(line), file)) {
        fprintf(stderr, "ini.txt is empty.\n");
        fclose(file);
        return false;
    }

    int n = atoi(line);
    if (n <= 0) {
        fprintf(stderr, "Invalid puzzle size in ini.txt.\n");
        fclose(file);
        return false;
    }

    int len = n * n;
    int *state = malloc(sizeof(int) * (size_t)len);
    if (!state) {
        fprintf(stderr, "Failed to allocate puzzle state.\n");
        fclose(file);
        return false;
    }

    int index = 0;
    int blank_index = -1;

    while (fgets(line, sizeof(line), file)) {
        char *token = strtok(line, ",\n\r");
        while (token) {
            if (index >= len) {
                fprintf(stderr, "Too many values in ini.txt.\n");
                free(state);
                fclose(file);
                return false;
            }
            int value = atoi(token);
            state[index] = value;
            if (value == -1) {
                blank_index = index;
            }
            index++;
            token = strtok(NULL, ",\n\r");
        }
    }
    fclose(file);

    if (index != len) {
        fprintf(stderr, "Expected %d values in ini.txt, got %d.\n", len, index);
        free(state);
        return false;
    }
    if (blank_index == -1) {
        fprintf(stderr, "Blank tile (-1) not found in ini.txt.\n");
        free(state);
        return false;
    }

    *out_state = state;
    *out_n = n;
    *out_blank = blank_index;
    return true;
}

static bool read_moves(const char *path, char **out_moves, size_t *out_count) {
    FILE *file = fopen(path, "r");
    if (!file) {
        return false;
    }

    char line[MAX_LINE];
    size_t capacity = 64;
    size_t count = 0;
    char *moves = malloc(capacity);
    if (!moves) {
        fclose(file);
        return false;
    }

    while (fgets(line, sizeof(line), file)) {
        char *ptr = line;
        while (*ptr == ' ' || *ptr == '\t') {
            ptr++;
        }
        if (*ptr == '\0' || *ptr == '\n' || *ptr == '\r') {
            continue;
        }
        char move = *ptr;
        if (move == 'U' || move == 'D' || move == 'L' || move == 'R') {
            if (count == capacity) {
                capacity *= 2;
                char *new_moves = realloc(moves, capacity);
                if (!new_moves) {
                    free(moves);
                    fclose(file);
                    return false;
                }
                moves = new_moves;
            }
            moves[count++] = move;
        }
    }
    fclose(file);

    if (count == 0) {
        free(moves);
        return false;
    }

    *out_moves = moves;
    *out_count = count;
    return true;
}

static void write_moves(const char *path, const char *moves, size_t count) {
    FILE *file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "Failed to write move file: %s\n", path);
        return;
    }
    for (size_t i = 0; i < count; i++) {
        fprintf(file, "%c\n", moves[i]);
    }
    fclose(file);
}

static int count_inversions(const int *state, int len) {
    int inv = 0;
    for (int i = 0; i < len; i++) {
        if (state[i] == -1) {
            continue;
        }
        for (int j = i + 1; j < len; j++) {
            if (state[j] == -1) {
                continue;
            }
            if (state[i] > state[j]) {
                inv++;
            }
        }
    }
    return inv;
}

static bool is_solvable(const int *state, int n, int blank_index) {
    int len = n * n;
    int inversions = count_inversions(state, len);
    if (n % 2 == 1) {
        return (inversions % 2) == 0;
    }
    int blank_row_from_bottom = n - (blank_index / n);
    if (blank_row_from_bottom % 2 == 0) {
        return (inversions % 2) == 1;
    }
    return (inversions % 2) == 0;
}

static void make_solvable(int *state, int n, int *blank_index) {
    if (is_solvable(state, n, *blank_index)) {
        return;
    }
    int len = n * n;
    int first = -1;
    int second = -1;
    for (int i = 0; i < len; i++) {
        if (state[i] == -1) {
            continue;
        }
        if (first == -1) {
            first = i;
        } else {
            second = i;
            break;
        }
    }
    if (first != -1 && second != -1) {
        int temp = state[first];
        state[first] = state[second];
        state[second] = temp;
    }
}

static bool generate_ini_file(const char *path, int n) {
    if (n <= 1) {
        fprintf(stderr, "Puzzle size must be greater than 1.\n");
        return false;
    }
    int len = n * n;
    int *state = malloc(sizeof(int) * (size_t)len);
    if (!state) {
        fprintf(stderr, "Failed to allocate puzzle state.\n");
        return false;
    }
    for (int i = 0; i < len - 1; i++) {
        state[i] = i;
    }
    state[len - 1] = -1;

    srand((unsigned int)time(NULL));
    for (int i = len - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = state[i];
        state[i] = state[j];
        state[j] = temp;
    }

    int blank_index = 0;
    for (int i = 0; i < len; i++) {
        if (state[i] == -1) {
            blank_index = i;
            break;
        }
    }

    make_solvable(state, n, &blank_index);

    FILE *file = fopen(path, "w");
    if (!file) {
        fprintf(stderr, "Failed to write %s: %s\n", path, strerror(errno));
        free(state);
        return false;
    }

    fprintf(file, "%d\n", n);
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            if (c > 0) {
                fprintf(file, ",");
            }
            fprintf(file, "%d", state[r * n + c]);
        }
        fprintf(file, "\n");
    }

    fclose(file);
    free(state);
    return true;
}

static int ida_search(SearchContext *ctx, int *state, int *blank_index, int g, int bound,
                      char prev_move, char *path) {
    int h = manhattan_distance(state, ctx->n);
    int f = g + h;
    if (f > bound) {
        return f;
    }
    if (is_goal(state, ctx->len)) {
        return -1;
    }

    ctx->expanded++;

    int min = INT_MAX;
    const char moves[4] = {'U', 'D', 'L', 'R'};
    for (int i = 0; i < 4; i++) {
        char move = moves[i];
        if (prev_move && move == opposite_move(prev_move)) {
            continue;
        }
        int prior_blank = *blank_index;
        if (!apply_move(state, ctx->n, blank_index, move)) {
            continue;
        }

        path[g] = move;
        int result = ida_search(ctx, state, blank_index, g + 1, bound, move, path);
        if (result == -1) {
            return -1;
        }
        if (result < min) {
            min = result;
        }

        apply_move(state, ctx->n, blank_index, opposite_move(move));
        *blank_index = prior_blank;
    }

    return min;
}

static void solve_puzzle(int *state, int n, int blank_index) {
    SearchContext ctx = {
        .n = n,
        .len = n * n,
        .expanded = 0
    };

    int bound = manhattan_distance(state, n);
    char *path = malloc(sizeof(char) * (size_t)MAX_ITERATION_BOUND);
    if (!path) {
        fprintf(stderr, "Failed to allocate solution path.\n");
        return;
    }

    while (true) {
        if (bound > MAX_ITERATION_BOUND) {
            printf("Search bound exceeded %d. No solution found.\n", MAX_ITERATION_BOUND);
            break;
        }
        int result = ida_search(&ctx, state, &blank_index, 0, bound, '\0', path);
        if (result == -1) {
            printf("Shortest solution length: %d moves\n", bound);
            printf("Tiles out of place: %d\n", count_misplaced(state, ctx.len));
            write_moves("move.txt", path, (size_t)bound);
            break;
        }
        if (result == INT_MAX) {
            printf("No solution found.\n");
            break;
        }
        bound = result;
    }

    printf("States expanded: %lld\n", ctx.expanded);
    free(path);
}

int main(int argc, char **argv) {
    if (argc >= 3 && strcmp(argv[1], "generate") == 0) {
        int n = atoi(argv[2]);
        if (!generate_ini_file("ini.txt", n)) {
            return EXIT_FAILURE;
        }
        printf("Generated ini.txt for %dx%d puzzle.\n", n, n);
        return EXIT_SUCCESS;
    }

    int *state = NULL;
    int n = 0;
    int blank_index = -1;

    if (!read_ini("ini.txt", &state, &n, &blank_index)) {
        return EXIT_FAILURE;
    }

    char *moves = NULL;
    size_t move_count = 0;
    if (read_moves("move.txt", &moves, &move_count)) {
        for (size_t i = 0; i < move_count; i++) {
            if (!apply_move(state, n, &blank_index, moves[i])) {
                fprintf(stderr, "Invalid move at line %zu.\n", i + 1);
                free(state);
                free(moves);
                return EXIT_FAILURE;
            }
        }
        printf("Final state after applying move.txt:\n");
        print_state(state, n);
        printf("Tiles out of place: %d\n", count_misplaced(state, n * n));
        free(moves);
    } else {
        printf("move.txt empty or missing. Solving with divide-and-conquer search (IDA*).\n");
        printf("Initial tiles out of place: %d\n", count_misplaced(state, n * n));
        solve_puzzle(state, n, blank_index);
    }

    free(state);
    return EXIT_SUCCESS;
}
