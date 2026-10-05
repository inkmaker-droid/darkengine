#ifndef __MFMOVIE_H
#define __MFMOVIE_H

// Plays an H.264/AAC MP4 sibling of a legacy movie through Windows Media
// Foundation. Returns FALSE when no modern sibling exists or setup fails so
// the caller can retain the original DirectShow fallback.
BOOL ModernMoviePlaySynchronous(const char *legacyPath, int volume);

#endif
