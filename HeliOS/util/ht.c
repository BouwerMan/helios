/**
 * @file ht.c
 * @brief Implementation of a hash table for the HeliOS project.
 *
 * This file contains the implementation of a hash table, including functions
 * for creating, destroying, and manipulating hash tables. The hash table uses
 * linear probing for collision resolution and supports dynamic resizing.
 *
 * Implementation is modified from https://benhoyt.com/writings/hash-table-in-c/
 *
 * @author Dylan Parks
 * @date 2025-04-05
 * @license GPL-3.0
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <kernel/liballoc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <util/ht.h>

// TODO: Implement some sort of LRU deletion
// TODO: Implement any sort of removal using custom destructors

static uint32_t hash_key(const char* key);
static const char* ht_set_entry(struct ht_entry* entries, size_t capacity,
                                const char* key, void* value, size_t* plength);
static bool ht_expand(struct ht* table);

#define INITIAL_CAPACITY 16 // Initial capacity of any hash tables created

/**
 * @brief Creates a new hash table.
 *
 * This function allocates and initializes a hash table structure. The hash
 * table uses the specified destructor function to clean up entries when the
 * table is destroyed or an entry is removed.
 *
 * @param destructor A pointer to a function that will be called to free the
 *                   memory of each entry in the hash table. Can be NULL if no
 *                   cleanup is needed.
 *
 * @return Pointer to the newly created hash table, or NULL if memory allocation
 *         fails.
 *
 * @note The caller is responsible for freeing the hash table and its entries
 *       using an appropriate cleanup function.
 */
struct ht* ht_create(void (*destructor)(void* entry))
{
    struct ht* table = kmalloc(sizeof(struct ht));
    if (table == NULL) return NULL;

    table->length = 0;
    table->capacity = INITIAL_CAPACITY;
    table->destructor = destructor;
    table->entries = kcalloc(table->capacity, sizeof(ht_entry));
    if (table->entries == NULL) {
        kfree(table);
        return NULL;
    }

    return table;
}

/**
 * @brief Frees all memory allocated for the hash table, including keys and
 * values.
 *
 * This function iterates through all entries in the hash table, freeing the
 * keys and values if a custom destructor is provided. It then frees the memory
 * allocated for the entries array and the hash table itself.
 *
 * @param table Pointer to the hash table to be destroyed.
 */
void ht_destroy(struct ht* table)
{
    // Free allocated keys and values if they have a custom destructor
    for (size_t i = 0; i < table->capacity; i++) {
        if (table->destructor) table->destructor(table->entries[i].value);
        kfree((void*)table->entries[i].key);
    }

    kfree(table->entries);
    kfree(table);
}

/**
 * @brief Retrieves the value associated with a given key in the hash table.
 *
 * This function searches the hash table for the specified key using linear
 * probing. If the key is found, the associated value is returned. If the key is
 * not found, the function returns NULL.
 *
 * @param table Pointer to the hash table.
 * @param key NUL-terminated string representing the key to search for.
 * @return Pointer to the value associated with the key, or NULL if the key is
 *         not found.
 */
void* ht_get(struct ht* table, const char* key)
{
    // AND hash with capacity-1 to ensure it's within entries array.
    // Equivalent to hash % capacity (maybe faster???)
    uint32_t hash = hash_key(key);
    size_t index = (size_t)(hash & (uint32_t)(table->capacity - 1));

    while (table->entries[index].key != NULL) {
        if (strcmp(key, table->entries[index].key) == 0) {
            // Found key
            return table->entries[index].value;
        }
        // Key wasn't in this slot so we increment (linear probing)
        index++;
        // At end of entries, wrap around
        if (index >= table->capacity) index = 0;
    }
    return NULL;
}

/**
 * @brief Sets a key-value pair in the hash table.
 *
 * This function associates the given key with the specified value in the hash
 * table. If the key is not already present, it is copied to newly allocated
 * memory. Keys are automatically freed when `ht_destroy` is called. If the
 * table's length exceeds half of its current capacity, the table is expanded.
 *
 * @param table Pointer to the hash table.
 * @param key   NUL-terminated string representing the key. The key is copied if
 *              not present.
 * @param value Pointer to the value to associate with the key. Must not be
 *              NULL.
 *
 * @return Pointer to the copied key, or NULL if out of memory
 *         or if value is NULL.
 */
const char* ht_set(struct ht* table, const char* key, void* value)
{
    if (value == NULL) {
        puts("WOAH YOU SHOULD SPECIFY A HT VALUE BIG MAN");
        return NULL;
    }

    // If length will exceed half of current capacity, expand it
    if (table->length >= table->capacity / 2) {
        if (!ht_expand(table)) return NULL;
    }

    return ht_set_entry(table->entries, table->capacity, key, value,
                        &table->length);
}

/**
 * @brief Retrieves the number of items in the hash table.
 *
 * This function returns the total number of key-value pairs currently stored
 * in the hash table.
 *
 * @param table Pointer to the hash table.
 * @return The number of items in the hash table.
 */
size_t ht_length(struct ht* table) { return table->length; }

/**
 * @brief Creates a new iterator for the hash table.
 *
 * This function initializes and returns an iterator for the specified hash
 * table. The iterator starts at the beginning of the table and can be used with
 * `ht_next` to traverse its entries.
 *
 * @param table Pointer to the hash table for which the iterator is created.
 *
 * @return A new hash table iterator.
 */
struct ht_iter ht_iterator(struct ht* table)
{
    struct ht_iter it;
    it._table = table;
    it._index = 0;
    return it;
}

/**
 * @brief Advances the iterator to the next item in the hash table.
 *
 * This function moves the iterator to the next non-empty entry in the hash
 * table. It updates the iterator's `key` and `value` fields to the current
 * item's key and value. If there are no more items, the function returns
 * `false`.
 *
 * @note: Do not modify the hash table (e.g., using `ht_set`) while iterating.
 *
 * @param it Pointer to the hash table iterator.
 * @return `true` if the iterator was advanced to a valid item, `false` if no
 *          more items exist.
 */
bool ht_next(struct ht_iter* it)
{
    // Loop till we've hit end of entries array
    struct ht* table = it->_table;
    while (it->_index < table->capacity) {
        size_t i = it->_index;
        it->_index++;
        if (table->entries[i].key != NULL) {
            // FOUND next non-empty item. update iterator key and value
            struct ht_entry entry = table->entries[i];
            it->key = entry.key;
            it->value = entry.value;
            return true;
        }
    }
    return false;
}

/***********************************
 * Static functions
 ***********************************/

#define FNV_PRIME  0x01000193 ///< The FNV prime constant.
#define FNV_OFFSET 0x811c9dc5 ///< The FNV offset basis constant.

// https://en.wikipedia.org/wiki/Fowler%E2%80%93Noll%E2%80%93Vo_hash_function
/**
 * @brief Computes the hash value for a given key using the FNV-1a algorithm.
 *
 * This function implements the Fowler–Noll–Vo hash function (FNV-1a) to compute
 * a 32-bit hash value for the provided key. It is designed for fast and
 * efficient hashing of strings.
 *
 * @param key NUL-terminated string to be hashed.
 * @return 32-bit hash value of the input key.
 */
static uint32_t hash_key(const char* key)
{
    uint32_t hash = FNV_OFFSET;
    for (const char* p = key; *p; p++) {
        hash ^= (uint32_t)(unsigned char)(*p);
        hash *= FNV_PRIME;
    }
    return hash;
}

/**
 * @brief Sets a key-value pair in the hash table entries array.
 *
 * This function inserts or updates a key-value pair in the hash table's entries
 * array. If the key already exists, its associated value is updated. If the key
 * does not exist, it is added to the table. The function handles collisions
 * using linear probing.
 *
 * @param entries   Pointer to the hash table's entries array.
 * @param capacity  The capacity of the hash table.
 * @param key       NUL-terminated string representing the key. The key is
 *                  duplicated if added.
 * @param value     Pointer to the value to associate with the key.
 * @param plength   Pointer to the current length of the hash table, or NULL if
 *                  not needed.
 *
 * @return Pointer to the key in the hash table,
 *         or NULL if memory allocation fails.
 */
static const char* ht_set_entry(struct ht_entry* entries, size_t capacity,
                                const char* key, void* value, size_t* plength)
{
    uint32_t hash = hash_key(key);
    size_t index = (size_t)(hash & (uint32_t)(capacity - 1));

    // Looping until empty entry
    while (entries[index].key != NULL) {
        // Check if key exists and updates value if so
        if (strcmp(key, entries[index].key) == 0) {
            entries[index].value = value;
            return entries[index].key;
        }
        // Key was not in this spot and it was full
        index++;
        // Wrapping around end of entries
        if (index >= capacity) index = 0;
    }

    if (plength != NULL) {
        key = strdup(key);
        if (key == NULL) return NULL;
        (*plength)++;
    }
    entries[index].key = (char*)key;
    entries[index].value = value;
    return key;
}

/**
 * @brief Expands the hash table to twice its current capacity.
 *
 * This function resizes the hash table by allocating a new entries array with
 * double the current capacity and rehashing all existing key-value pairs into
 * the new array. It ensures that the hash table can accommodate more entries
 * without significant performance degradation due to collisions.
 *
 * @param table Pointer to the hash table to be expanded.
 *
 * @return `true` if the expansion is successful,
 *         `false` if memory allocation fails.
 */
static bool ht_expand(struct ht* table)
{
    size_t new_capacity = table->capacity * 2;
    if (new_capacity < table->capacity) return false; // overflow
    struct ht_entry* new_entries
        = kcalloc(new_capacity, sizeof(struct ht_entry));
    if (new_entries == NULL) return false;

    // Iterate entries, move non-empty to new table
    for (size_t i = 0; i < table->capacity; i++) {
        struct ht_entry entry = table->entries[i];
        if (entry.key != NULL) {
            ht_set_entry(new_entries, new_capacity, entry.key, entry.value,
                         NULL);
        }
    }

    // Free old entries and update table details
    // Don't need destructor here because we keep a reference in new table
    kfree(table->entries);
    table->entries = new_entries;
    table->capacity = new_capacity;
    return true;
}
