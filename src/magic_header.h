#ifndef H_MAGIC_HEADER
#define H_MAGIC_HEADER

#include <linux/types.h>

struct magic_header {
    u32 start;
    u32 end;
};

int mh_parse(struct magic_header *mh, char *desc);
int mh_genspec(struct magic_header *mh, char *desc, size_t buflen);

bool mh_validate(__le32 received, struct magic_header* mh);
u32 mh_genheader(struct magic_header* mh);

/* Header for a peer: AWG 1.0 peers only know one value per message type,
 * the range start; everyone else gets a random value from the range.
 */
static inline u32 mh_peerheader(struct magic_header *mh, bool fixed)
{
    return fixed ? mh->start : mh_genheader(mh);
}

#endif