#ifndef BIN64_H
#define BIN64_H
#include <assert.h>
#include <stdint.h>

#include <stdio.h>

/** Bin numbers in the tail111 encoding: meaningless
    bits in the tail are set to 0111...11, while the
    head denotes the offset. Thus, 1101 is the bin
    at layer 1, offset 3 (i.e. fourth). */
struct bin64_t {
    uint64_t v;
    static const uint64_t NONE = 0xffffffffffffffffULL;
    static const uint64_t ALL = 0x7fffffffffffffffULL;

    bin64_t() : v(NONE) {}
    bin64_t(const bin64_t&b) : v(b.v) {}
    bin64_t(const uint64_t val) : v(val) {}
    bin64_t(uint8_t layer, uint64_t offset) : 
        v( (offset<<(layer+1)) | ((1ULL<<layer)-1) ) {}
    operator uint64_t () const { return v; }
    bool operator == (bin64_t& b) const { return v==b.v; }

    static bin64_t none () { return NONE; }
    static bin64_t all () { return ALL; }

    uint64_t tail_bits () const {
        return v ^ (v+1);
    }

    uint64_t tail_bit () const {
        return (tail_bits()+1)>>1;
    }

    bin64_t sibling () const {
        // if (v==ALL) return NONE; 
        return bin64_t(v^(tail_bit()<<1));
    }

    int layer () const {
        int r = 0;
        uint64_t tail = ((v^(v+1))+1)>>1;
        if (tail>0xffffffffULL) {
            r = 32;
            tail>>=32;
        }
        // courtesy of Sean Eron Anderson
        // http://graphics.stanford.edu/~seander/bithacks.html
        static const int DeBRUIJN[32] = {
          0, 1, 28, 2, 29, 14, 24, 3, 30, 22, 20, 15, 25, 17, 4, 8, 
          31, 27, 13, 23, 21, 19, 16, 7, 26, 12, 18, 6, 11, 5, 10, 9
        };
        r += DeBRUIJN[((uint32_t)(tail*0x077CB531U))>>27];
        return r;
    }

    uint64_t base_offset () const {
        return (v&~(tail_bits()))>>1;
    }

    uint64_t offset () const {
        return v >> (layer()+1);
    }

    bin64_t to (bool right) const {
        if (!(v&1))
            return NONE;
        uint64_t tb = tail_bit()>>1;
        if (right)
            tb |= (tb<<1);
        return bin64_t(v^tb);
    }

    bin64_t left () const {
        return to(false);
    }

    bin64_t right () const {
        return to(true);
    }

    bool    within (bin64_t maybe_asc) {
        uint64_t short_tail = maybe_asc.tail_bits();
        if (tail_bits()>short_tail)
            return false;
        return (v&~short_tail) == (maybe_asc.v&~short_tail) ;
    }

    bin64_t towards (bin64_t desc) const {
        if (!desc.within(*this))
            return NONE;
        if (desc.within(left()))
            return left();
        else
            return right();
    }

    bin64_t parent () const {
        uint64_t tbs = tail_bits(), ntbs = (tbs+1)|tbs;
        return bin64_t( (v&~ntbs) | tbs );
    }

    bool is_left () const {
        uint64_t tb = tail_bit();
        return !(v&(tb<<1));
    }
    bool is_right() const { return !is_left(); }

    /** The array must have 64 cells, as it is the max
     number of peaks possible (and there are no reason
     to assume there will be less in any given case. */
    static void GetPeaks(uint64_t length, bin64_t* peaks) {
        int pp=0;
        uint8_t layer = 0;
        while (length) {
            if (length&1) 
                peaks[pp++] = bin64_t(layer,length^1);
            length>>=1;
            layer++;
        }
        peaks[pp] = NONE;
    }

};


#endif

/**
                 00111
       0011                    1011
  001      101         1001          1101
0   10  100  110    1000  1010   1100   1110

                  7
      3                         11
  1        5             9             13
0   2    4    6       8    10      12     14

once we have peak hashes, this struture is more natural than bin-v1

*/
