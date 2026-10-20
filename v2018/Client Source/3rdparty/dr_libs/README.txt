dr_mp3.h v0.7.4 (development version after v0.7.3; public domain or MIT-0), from
https://github.com/mackron/dr_libs commit 51e61d308dde6b437fce0c5fabb32cd86b40f4d7 (2026-08-30,
includes the fix for a heap overflow when parsing Xing/Info tags). Used outside Windows by
CSoundDecoder (Engine/Sound/SoundDecoder.cpp) for .mp3 streams, in place of amp11lib.dll.
