#include "compiler/common.h"
#include "compiler/BitVectors.h"

void bitvectorunion(UInt32 *destination, const UInt32 *source, int bitCount)
{
    int wordCount = (bitCount + 31) >> 5;

    while (wordCount != 0) {
        *destination++ |= *source++;
        wordCount--;
    }
}

void bitvectorinitialize(UInt32 *words, int bitCount, UInt32 value)
{
    int wordCount = (bitCount + 31) >> 5;

    while (wordCount != 0) {
        *words++ = value;
        wordCount--;
    }
}

/* Copies bit_count bits rounded up to a whole word count. */
SInt32 BitVectors_CopyAndCheckChanged(UInt32 *destination, UInt32 *source, SInt32 bitCount)
{
    unsigned int changed;
    const UInt32 *nextSource;
    UInt32 *nextDestination;
    unsigned int remaining;
    unsigned int word;
    unsigned int count;
    int empty;
    int equal;
    int more;
    count = bitCount;
    count += 31U;
    nextDestination = destination;
    count = (int)count >> 5;
    nextSource = source;
    remaining = count;
    changed = 0U;
    empty = (count == 0U);
    if (!empty) {
        do {
            word = *nextSource;
            equal = (*nextDestination == word);
            count = word;
            if (!equal)
                changed = 1U;
            remaining -= 1U;
            *nextDestination = count;
            nextDestination++;
            nextSource++;
            more = (remaining != 0U);
        } while (more);
    }
    return changed;
}

void bitvectorcopy(UInt32 *destination, const UInt32 *source, int bit_count)
{
    int count = (bit_count + 31) >> 5;

    while (count != 0) {
        *destination++ = *source++;
        count--;
    }
}

#ifdef __MWERKS__
#endif

#ifdef __MWERKS__
#endif
