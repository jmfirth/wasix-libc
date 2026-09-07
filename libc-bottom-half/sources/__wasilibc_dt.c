#include <__header_dirent.h>
#include <__mode_t.h>

int __wasilibc_iftodt(int x) {
    switch (x) {
        case S_IFDIR: return DT_DIR;
        case S_IFCHR: return DT_CHR;
        case S_IFBLK: return DT_BLK;
        case S_IFREG: return DT_REG;
        case S_IFIFO: return DT_FIFO;
        case S_IFLNK: return DT_LNK;
#ifdef DT_SOCK
        case S_IFSOCK: return DT_SOCK;
#endif
        default: return DT_UNKNOWN;
    }
}

int __wasilibc_dttoif(int x) {
    switch (x) {
        case DT_DIR: return S_IFDIR;
        case DT_CHR: return S_IFCHR;
        case DT_BLK: return S_IFBLK;
        case DT_REG: return S_IFREG;
        case DT_FIFO: return S_IFIFO;
        case DT_LNK: return S_IFLNK;
#ifdef DT_SOCK
        case DT_SOCK: return S_IFSOCK;
#endif
        /* WASI has two socket filetypes and only DT_FIFO aliases one of them
         * (__WASI_FILETYPE_SOCKET_STREAM), so a datagram socket's d_type
         * reaches here as a bare filetype with no DT_* spelling. It IS a
         * socket; say so explicitly rather than let the default arm answer. */
        case __WASI_FILETYPE_SOCKET_DGRAM: return S_IFSOCK;
        /* firebox#Z64: DT_UNKNOWN means "the filesystem did not tell me what
         * this is". Answering S_IFSOCK converts that admission of ignorance
         * into a confident wrong type, and every caller that does
         * `st_mode = DTTOIF(ent->d_type)` then takes the socket branch for
         * ordinary files — invariant 0's worst class, a false success.
         * Before firebox#NJ4 this at least landed on 0160000, a mode word no
         * Linux inode carries; NJ4 moved S_IFSOCK onto Linux's 0140000, which
         * made the fabrication INDISTINGUISHABLE from a real socket.
         * Linux/glibc/musl define `DTTOIF(x) ((x) << 12)`, so DT_UNKNOWN (0)
         * maps to 0: no type bits, every S_IS*() answers false, and the caller
         * learns exactly what the filesystem knew. Match that. The same answer
         * is the honest one for any filetype this switch does not recognise —
         * our DT_* namespace is WASI's, so an unlisted value has no width to
         * shift into a meaningful type. */
        case DT_UNKNOWN:
        default:
            return 0;
    }
}
