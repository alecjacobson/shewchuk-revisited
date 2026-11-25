/* Renames every external symbol of reference/predicates.c.
 *
 * Included by shewchuk_ref.c before the pristine file, so that the 1996
 * definitions get a prefix (SHEWCHUK_REF_PREFIX) and can be linked next to
 * other definitions of orient2d, exactinit, ... (new code, Triangle, TetGen).
 * The list is checked against the built library by the
 * reference.symbol_prefix test. */
#ifndef SHEWCHUK_REF_RENAME_H
#define SHEWCHUK_REF_RENAME_H

#ifndef SHEWCHUK_REF_PREFIX
#error "SHEWCHUK_REF_PREFIX must be defined (the build system sets it)"
#endif

#define SHEWCHUK_REF_PASTE2(a, b) a##b
#define SHEWCHUK_REF_PASTE(a, b) SHEWCHUK_REF_PASTE2(a, b)
#define SHEWCHUK_REF_NAME(name) SHEWCHUK_REF_PASTE(SHEWCHUK_REF_PREFIX, name)

#define ccwerrboundA SHEWCHUK_REF_NAME(ccwerrboundA)
#define ccwerrboundB SHEWCHUK_REF_NAME(ccwerrboundB)
#define ccwerrboundC SHEWCHUK_REF_NAME(ccwerrboundC)
#define compress SHEWCHUK_REF_NAME(compress)
#define doublerand SHEWCHUK_REF_NAME(doublerand)
#define epsilon SHEWCHUK_REF_NAME(epsilon)
#define estimate SHEWCHUK_REF_NAME(estimate)
#define exactinit SHEWCHUK_REF_NAME(exactinit)
#define expansion_sum SHEWCHUK_REF_NAME(expansion_sum)
#define expansion_sum_zeroelim1 SHEWCHUK_REF_NAME(expansion_sum_zeroelim1)
#define expansion_sum_zeroelim2 SHEWCHUK_REF_NAME(expansion_sum_zeroelim2)
#define fast_expansion_sum SHEWCHUK_REF_NAME(fast_expansion_sum)
#define fast_expansion_sum_zeroelim SHEWCHUK_REF_NAME(fast_expansion_sum_zeroelim)
#define floatrand SHEWCHUK_REF_NAME(floatrand)
#define grow_expansion SHEWCHUK_REF_NAME(grow_expansion)
#define grow_expansion_zeroelim SHEWCHUK_REF_NAME(grow_expansion_zeroelim)
#define iccerrboundA SHEWCHUK_REF_NAME(iccerrboundA)
#define iccerrboundB SHEWCHUK_REF_NAME(iccerrboundB)
#define iccerrboundC SHEWCHUK_REF_NAME(iccerrboundC)
#define incircle SHEWCHUK_REF_NAME(incircle)
#define incircleadapt SHEWCHUK_REF_NAME(incircleadapt)
#define incircleexact SHEWCHUK_REF_NAME(incircleexact)
#define incirclefast SHEWCHUK_REF_NAME(incirclefast)
#define incircleslow SHEWCHUK_REF_NAME(incircleslow)
#define insphere SHEWCHUK_REF_NAME(insphere)
#define insphereadapt SHEWCHUK_REF_NAME(insphereadapt)
#define insphereexact SHEWCHUK_REF_NAME(insphereexact)
#define inspherefast SHEWCHUK_REF_NAME(inspherefast)
#define insphereslow SHEWCHUK_REF_NAME(insphereslow)
#define isperrboundA SHEWCHUK_REF_NAME(isperrboundA)
#define isperrboundB SHEWCHUK_REF_NAME(isperrboundB)
#define isperrboundC SHEWCHUK_REF_NAME(isperrboundC)
#define linear_expansion_sum SHEWCHUK_REF_NAME(linear_expansion_sum)
#define linear_expansion_sum_zeroelim SHEWCHUK_REF_NAME(linear_expansion_sum_zeroelim)
#define narrowdoublerand SHEWCHUK_REF_NAME(narrowdoublerand)
#define narrowfloatrand SHEWCHUK_REF_NAME(narrowfloatrand)
#define o3derrboundA SHEWCHUK_REF_NAME(o3derrboundA)
#define o3derrboundB SHEWCHUK_REF_NAME(o3derrboundB)
#define o3derrboundC SHEWCHUK_REF_NAME(o3derrboundC)
#define orient2d SHEWCHUK_REF_NAME(orient2d)
#define orient2dadapt SHEWCHUK_REF_NAME(orient2dadapt)
#define orient2dexact SHEWCHUK_REF_NAME(orient2dexact)
#define orient2dfast SHEWCHUK_REF_NAME(orient2dfast)
#define orient2dslow SHEWCHUK_REF_NAME(orient2dslow)
#define orient3d SHEWCHUK_REF_NAME(orient3d)
#define orient3dadapt SHEWCHUK_REF_NAME(orient3dadapt)
#define orient3dexact SHEWCHUK_REF_NAME(orient3dexact)
#define orient3dfast SHEWCHUK_REF_NAME(orient3dfast)
#define orient3dslow SHEWCHUK_REF_NAME(orient3dslow)
#define resulterrbound SHEWCHUK_REF_NAME(resulterrbound)
#define scale_expansion SHEWCHUK_REF_NAME(scale_expansion)
#define scale_expansion_zeroelim SHEWCHUK_REF_NAME(scale_expansion_zeroelim)
#define splitter SHEWCHUK_REF_NAME(splitter)
#define uniformdoublerand SHEWCHUK_REF_NAME(uniformdoublerand)
#define uniformfloatrand SHEWCHUK_REF_NAME(uniformfloatrand)

#endif
