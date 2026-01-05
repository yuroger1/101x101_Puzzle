#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <errno.h>

#define MAX_LINE 8192

typedef struct Node {
    int *state;
    int blank_index;
    int g;
    int h;
    struct Node *parent;
    char move;
} Node;

typedef struct {
    Node **data;
    size_t size;
    size_t capacity;
} MinHeap;

typedef struct VisitedEntry {
    uint64_t hash;
    int g;
    int *state;
    struct VisitedEntry *next;
} VisitedEntry;

typedef struct {
    VisitedEntry **buckets;
    size_t bucket_count;
} VisitedSet;

static uint64_t fnv1a_hash(const int *state, int len) {
    uint64_t hash = 1469598103934665603ULL;
    for (int i = 0; i < len; i++) {
        uint64_t value = (uint64_t)(state[i] + 2);
        hash ^= value + 0x9e3779b97f4a7c15ULL + (hash << 6) + (hash >> 2);
        hash *= 1099511628211ULL;
    }
    return hash;
}

static bool states_equal(const int *a, const int *b, int len) {
    return memcmp(a, b, sizeof(int) * (size_t)len) == 0;
}

static void heap_init(MinHeap *heap) {
    heap->data = NULL;
    heap->size = 0;
    heap->capacity = 0;
}

static void heap_swap(Node **a, Node **b) {
    Node *tmp = *a;
    *a = *b;
    *b = tmp;
}

static int node_priority(const Node *node) {
    return node->g + node->h;
}

static void heap_push(MinHeap *heap, Node *node) {
    if (heap->size == heap->capacity) {
        size_t new_capacity = heap->capacity == 0 ? 64 : heap->capacity * 2;
        Node **new_data = realloc(heap->data, new_capacity * sizeof(Node *));
        if (!new_data) {
            fprintf(stderr, "Failed to allocate heap.\n");
            exit(EXIT_FAILURE);
        }
        heap->data = new_data;
        heap->capacity = new_capacity;
    }

    heap->data[heap->size] = node;
    size_t idx = heap->size;
    heap->size++;

    while (idx > 0) {
        size_t parent = (idx - 1) / 2;
        int cur_priority = node_priority(heap->data[idx]);
        int parent_priority = node_priority(heap->data[parent]);
        if (cur_priority < parent_priority ||
            (cur_priority == parent_priority && heap->data[idx]->h < heap->data[parent]->h)) {
            heap_swap(&heap->data[idx], &heap->data[parent]);
            idx = parent;
        } else {
            break;
        }
    }
}

static Node *heap_pop(MinHeap *heap) {
    if (heap->size == 0) {
        return NULL;
    }
    Node *top = heap->data[0];
    heap->size--;
    if (heap->size > 0) {
        heap->data[0] = heap->data[heap->size];
        size_t idx = 0;
        while (true) {
            size_t left = idx * 2 + 1;
            size_t right = idx * 2 + 2;
            size_t smallest = idx;
            if (left < heap->size) {
                int left_priority = node_priority(heap->data[left]);
                int smallest_priority = node_priority(heap->data[smallest]);
                if (left_priority < smallest_priority ||
                    (left_priority == smallest_priority &&
                     heap->data[left]->h < heap->data[smallest]->h)) {
                    smallest = left;
                }
            }
            if (right < heap->size) {
                int right_priority = node_priority(heap->data[right]);
                int smallest_priority = node_priority(heap->data[smallest]);
                if (right_priority < smallest_priority ||
                    (right_priority == smallest_priority &&
                     heap->data[right]->h < heap->data[smallest]->h)) {
                    smallest = right;
                }
            }
            if (smallest != idx) {
                heap_swap(&heap->data[idx], &heap->data[smallest]);
                idx = smallest;
            } else {
                break;
            }
        }
    }
    return top;
}

static void visited_init(VisitedSet *set, size_t bucket_count) {
    set->bucket_count = bucket_count;
    set->buckets = calloc(bucket_count, sizeof(VisitedEntry *));
    if (!set->buckets) {
        fprintf(stderr, "Failed to allocate visited set.\n");
        exit(EXIT_FAILURE);
    }
}

static bool visited_should_skip(VisitedSet *set, uint64_t hash, const int *state, int len, int g) {
    size_t idx = hash % set->bucket_count;
    VisitedEntry *entry = set->buckets[idx];
    while (entry) {
        if (entry->hash == hash && states_equal(entry->state, state, len)) {
            if (g >= entry->g) {
                return true;
            }
            entry->g = g;
            return false;
        }
        entry = entry->next;
    }
    return false;
}

static void visited_add(VisitedSet *set, uint64_t hash, int *state, int len, int g) {
    size_t idx = hash % set->bucket_count;
    VisitedEntry *entry = malloc(sizeof(VisitedEntry));
    if (!entry) {
        fprintf(stderr, "Failed to allocate visited entry.\n");
        exit(EXIT_FAILURE);
    }
    entry->hash = hash;
    entry->g = g;
    entry->state = state;
    entry->next = set->buckets[idx];
    set->buckets[idx] = entry;
}

static void visited_free(VisitedSet *set) {
    for (size_t i = 0; i < set->bucket_count; i++) {
        VisitedEntry *entry = set->buckets[i];
        while (entry) {
            VisitedEntry *next = entry->next;
            free(entry);
            entry = next;
        }
    }
    free(set->buckets);
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

static void print_state(const int *state, int n) {
    for (int r = 0; r < n; r++) {
        for (int c = 0; c < n; c++) {
            int value = state[r * n + c];
            if (c > 0) {
                printf(",");
            }
            printf("%d", value);
        }
        printf("\n");
    }
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

static Node *create_node(const int *state, int len, int blank_index, int g, int h, Node *parent, char move) {
    Node *node = malloc(sizeof(Node));
    if (!node) {
        fprintf(stderr, "Failed to allocate node.\n");
        exit(EXIT_FAILURE);
    }
    node->state = malloc(sizeof(int) * (size_t)len);
    if (!node->state) {
        fprintf(stderr, "Failed to allocate node state.\n");
        exit(EXIT_FAILURE);
    }
    memcpy(node->state, state, sizeof(int) * (size_t)len);
    node->blank_index = blank_index;
    node->g = g;
    node->h = h;
    node->parent = parent;
    node->move = move;
    return node;
}

typedef struct {
    Node **items;
    size_t size;
    size_t capacity;
} NodeList;

static void node_list_init(NodeList *list) {
    list->items = NULL;
    list->size = 0;
    list->capacity = 0;
}

static void node_list_push(NodeList *list, Node *node) {
    if (list->size == list->capacity) {
        size_t new_capacity = list->capacity == 0 ? 128 : list->capacity * 2;
        Node **new_items = realloc(list->items, new_capacity * sizeof(Node *));
        if (!new_items) {
            fprintf(stderr, "Failed to allocate node list.\n");
            exit(EXIT_FAILURE);
        }
        list->items = new_items;
        list->capacity = new_capacity;
    }
    list->items[list->size++] = node;
}

static void node_list_free(NodeList *list) {
    for (size_t i = 0; i < list->size; i++) {
        free(list->items[i]->state);
        free(list->items[i]);
    }
    free(list->items);
}

static char *reconstruct_moves(Node *goal_node, size_t *out_count) {
    size_t capacity = (size_t)goal_node->g + 1;
    char *moves = malloc(capacity);
    if (!moves) {
        fprintf(stderr, "Failed to allocate moves.\n");
        exit(EXIT_FAILURE);
    }

    size_t idx = 0;
    Node *current = goal_node;
    while (current->parent) {
        moves[idx++] = current->move;
        current = current->parent;
    }

    for (size_t i = 0; i < idx / 2; i++) {
        char temp = moves[i];
        moves[i] = moves[idx - 1 - i];
        moves[idx - 1 - i] = temp;
    }

    *out_count = idx;
    return moves;
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

static bool is_goal(const int *state, int len) {
    for (int i = 0; i < len - 1; i++) {
        if (state[i] != i) {
            return false;
        }
    }
    return state[len - 1] == -1;
}

static void solve_puzzle(const int *start_state, int n, int blank_index) {
    int len = n * n;
    int start_h = manhattan_distance(start_state, n);
    Node *start_node = create_node(start_state, len, blank_index, 0, start_h, NULL, '\0');
    NodeList nodes;
    node_list_init(&nodes);
    node_list_push(&nodes, start_node);

    MinHeap open_set;
    heap_init(&open_set);
    heap_push(&open_set, start_node);

    VisitedSet visited;
    visited_init(&visited, 1048576);
    visited_add(&visited, fnv1a_hash(start_state, len), start_node->state, len, 0);

    Node *goal_node = NULL;
    const char moves[4] = {'U', 'D', 'L', 'R'};

    while (open_set.size > 0) {
        Node *current = heap_pop(&open_set);
        if (is_goal(current->state, len)) {
            goal_node = current;
            break;
        }

        for (int i = 0; i < 4; i++) {
            char move = moves[i];
            if (current->parent && move == opposite_move(current->move)) {
                continue;
            }
            int *next_state = malloc(sizeof(int) * (size_t)len);
            if (!next_state) {
                fprintf(stderr, "Failed to allocate state.\n");
                exit(EXIT_FAILURE);
            }
            memcpy(next_state, current->state, sizeof(int) * (size_t)len);
            int next_blank = current->blank_index;
            if (!apply_move(next_state, n, &next_blank, move)) {
                free(next_state);
                continue;
            }

            int g = current->g + 1;
            uint64_t hash = fnv1a_hash(next_state, len);
            if (visited_should_skip(&visited, hash, next_state, len, g)) {
                free(next_state);
                continue;
            }

            int h = manhattan_distance(next_state, n);
            Node *child = create_node(next_state, len, next_blank, g, h, current, move);
            free(next_state);
            heap_push(&open_set, child);
            visited_add(&visited, hash, child->state, len, g);
            node_list_push(&nodes, child);
        }
    }

    if (!goal_node) {
        printf("No solution found.\n");
        visited_free(&visited);
        free(open_set.data);
        node_list_free(&nodes);
        return;
    }

    size_t move_count = 0;
    char *solution_moves = reconstruct_moves(goal_node, &move_count);
    printf("Shortest solution length: %zu moves\n", move_count);
    printf("Tiles out of place: %d\n", count_misplaced(goal_node->state, len));
    write_moves("move.txt", solution_moves, move_count);
    free(solution_moves);

    visited_free(&visited);
    free(open_set.data);
    node_list_free(&nodes);
}

int main(void) {
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
        printf("move.txt empty or missing. Solving with A* search...\n");
        printf("Initial tiles out of place: %d\n", count_misplaced(state, n * n));
        solve_puzzle(state, n, blank_index);
    }

    free(state);
    return EXIT_SUCCESS;
}
