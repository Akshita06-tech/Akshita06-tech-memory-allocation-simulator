#include "memory.h"
#include "policy.h"

static Block *new_block(int start, int size, int free_flag) {
    Block *b = (Block *)calloc(1, sizeof(Block));
    if (!b) return NULL;
    b->start = start; b->size = size; b->is_free = free_flag;
    return b;
}

Memory *mem_create(int units, int unit_bytes) {
    if (units <= 0 || unit_bytes <= 0) return NULL;
    Memory *m = (Memory *)calloc(1, sizeof(Memory));
    if (!m) return NULL;
    m->total_units = units; m->unit_bytes = unit_bytes;
    m->head = new_block(0, units, 1);
    if (!m->head) { free(m); return NULL; }
    return m;
}

void mem_destroy(Memory *m) {
    if (!m) return;
    Block *b = m->head;
    while (b) { Block *next = b->next; free(b); b = next; }
    free(m->pool); free(m);
}

Block *mem_alloc_selected(Memory *m, const char *owner, int units, Block *b) {
    if (!m || !owner || !*owner || strlen(owner)>=OWNER_LEN || units <= 0 ||
        units > m->total_units || !b || !b->is_free || b->size < units) return NULL;
    int belongs=0;
    for (Block *it=m->head; it; it=it->next) {
        if(it==b) belongs=1;
        if (!it->is_free && strcmp(it->owner, owner)==0) return NULL;
    }
    if(!belongs) return NULL;
    if (b->size > units) {
        Block *tail = new_block(b->start + units, b->size - units, 1);
        if (!tail) return NULL;
        tail->next = b->next; b->next = tail; b->size = units;
    }
    b->is_free = 0;
    snprintf(b->owner, sizeof(b->owner), "%s", owner);
    b->data_len = 0;
    return b;
}

Block *mem_alloc(Memory *m, const char *owner, int units, Strategy strategy) {
    int examined=0;
    Block *candidate=find_block(m,units,strategy,&examined);
    return mem_alloc_selected(m,owner,units,candidate);
}

static void merge_free(Block *b) {
    while (b && b->next) {
        if (b->is_free && b->next->is_free) {
            Block *dead = b->next;
            b->size += dead->size; b->next = dead->next; free(dead);
        } else b = b->next;
    }
}

int mem_free(Memory *m, const char *owner) {
    if (!m || !owner) return 0;
    for (Block *b = m->head; b; b = b->next) {
        if (!b->is_free && strcmp(b->owner, owner) == 0) {
            b->is_free = 1; b->owner[0] = '\0'; b->data_len = 0;
            merge_free(m->head);
            return 1;
        }
    }
    return 0;
}

void mem_compact(Memory *m) {
    if (!m) return;
    int used_total=0;
    for(Block *it=m->head;it;it=it->next) if(!it->is_free) used_total+=it->size;
    Block *free_node = used_total < m->total_units ? new_block(used_total,m->total_units-used_total,1) : NULL;
    if(used_total < m->total_units && !free_node) return; /* leave the old map untouched on allocation failure */
    int cursor = 0;
    Block *new_head = NULL, **tail = &new_head;
    for (Block *b = m->head; b;) {
        Block *next = b->next;
        if (!b->is_free) {
            if (b->start != cursor) {
                m->units_moved += b->size;
                if (m->pool)
                    memmove(m->pool + (size_t)cursor * m->unit_bytes,
                            m->pool + (size_t)b->start * m->unit_bytes,
                            (size_t)b->size * m->unit_bytes);
            }
            b->start = cursor; cursor += b->size; b->next = NULL;
            *tail = b; tail = &b->next;
        } else free(b);
        b = next;
    }
    if (free_node) { free_node->start=cursor; *tail=free_node; }
    m->head = new_head;
    m->compactions++;
}

int mem_lookup(const Memory *m, const char *owner, int *start, int *end) {
    if (!m || !owner) return 0;
    for (Block *b = m->head; b; b = b->next)
        if (!b->is_free && strcmp(b->owner, owner) == 0) {
            if (start) *start = b->start;
            if (end) *end = b->start + b->size;
            return 1;
        }
    return 0;
}

int mem_check_invariants(const Memory *m) {
    if (!m || !m->head) return 1;
    int expected = 0, free_total = 0, used_total = 0;
    const Block *prev = NULL;
    for (const Block *b = m->head; b; b = b->next) {
        if (b->start != expected || b->size <= 0) return 1;
        if (b->is_free) {
            if (b->owner[0] != '\0' || (prev && prev->is_free)) return 1;
            free_total += b->size;
        } else {
            if (b->owner[0] == '\0') return 1;
            used_total += b->size;
        }
        expected += b->size; prev = b;
    }
    return (expected == m->total_units && free_total + used_total == m->total_units) ? 0 : 1;
}

Stats mem_stats(const Memory *m) {
    Stats s = {0,0,0,0,0.0};
    if (!m) return s;
    for (Block *b = m->head; b; b = b->next) {
        if (b->is_free) {
            s.total_free += b->size; s.free_fragments++;
            if (b->size > s.largest_free) s.largest_free = b->size;
        } else s.used_memory += b->size;
    }
    s.external_fragmentation = s.total_free == 0 ? 0.0 :
        (1.0 - (double)s.largest_free / s.total_free) * 100.0;
    return s;
}

void mem_print_map(const Memory *m) {
    if (!m) return;
    printf("\nMEMORY MAP (pool %d KB)\n", m->total_units);
    for (Block *b = m->head; b; b = b->next)
        printf("[%-6d, %-6d) %-22s %-5s %d KB\n", b->start, b->start+b->size,
               b->is_free ? "(free)" : b->owner, b->is_free ? "FREE" : "USED", b->size);
    Stats s = mem_stats(m);
    printf("Used %d KB | Free %d KB | fragments %d | largest %d KB | external fragmentation %.2f%%\n",
           s.used_memory, s.total_free, s.free_fragments, s.largest_free, s.external_fragmentation);
}

int mem_write_bytes(Memory *m, const char *owner, const unsigned char *data, int len) {
    int start, end;
    if (!m || !data || len <= 0 || !mem_lookup(m, owner, &start, &end)) return 0;
    size_t cap = (size_t)(end-start) * m->unit_bytes;
    if ((size_t)len > cap) return 0;
    if (!m->pool) {
        m->pool = (unsigned char *)calloc((size_t)m->total_units, (size_t)m->unit_bytes);
        if (!m->pool) return 0;
    }
    memcpy(m->pool + (size_t)start*m->unit_bytes, data, (size_t)len);
    for (Block *b=m->head;b;b=b->next)
        if (!b->is_free && strcmp(b->owner,owner)==0) { b->data_len=len; break; }
    return 1;
}

int mem_read_bytes(const Memory *m, const char *owner, unsigned char *out, int capacity, int *actual_len) {
    int start, end;
    if (!m || !out || !m->pool || !mem_lookup(m,owner,&start,&end)) return 0;
    int len=0;
    for (Block *b=m->head;b;b=b->next)
        if (!b->is_free && strcmp(b->owner,owner)==0) { len=b->data_len; break; }
    if (len <= 0 || len > capacity || (size_t)len > (size_t)(end-start)*m->unit_bytes) return 0;
    memcpy(out,m->pool+(size_t)start*m->unit_bytes,(size_t)len);
    if (actual_len) *actual_len=len;
    return 1;
}
