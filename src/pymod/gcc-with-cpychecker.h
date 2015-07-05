
#ifndef __WITH_CPYCHECKER_H
#define __WITH_CPYCHECKER_H

/* pyinclude: pymod/gcc-with-cpychecker.h */

#if defined(WITH_CPYCHECKER_RETURNS_BORROWED_REF_ATTRIBUTE)
  #define CPYCHECKER_RETURNS_BORROWED_REF \
    __attribute__((cpychecker_returns_borrowed_ref))
#else
  #define CPYCHECKER_RETURNS_BORROWED_REF
#endif

#if defined(WITH_CPYCHECKER_STEALS_REFERENCE_TO_ARG_ATTRIBUTE)
  #define CPYCHECKER_STEALS_REFERENCE_TO_ARG(n) \
   __attribute__((cpychecker_steals_reference_to_arg(n)))
#else
 #define CPYCHECKER_STEALS_REFERENCE_TO_ARG(n)
#endif

#if defined(WITH_CPYCHECKER_SETS_EXCEPTION_ATTRIBUTE)
  #define CPYCHECKER_SETS_EXCEPTION \
     __attribute__((cpychecker_sets_exception))
#else
  #define CPYCHECKER_SETS_EXCEPTION
#endif

#if defined(WITH_CPYCHECKER_NEGATIVE_RESULT_SETS_EXCEPTION_ATTRIBUTE)
  #define CPYCHECKER_NEGATIVE_RESULT_SETS_EXCEPTION \
     __attribute__((cpychecker_negative_result_sets_exception))
#else
  #define CPYCHECKER_NEGATIVE_RESULT_SETS_EXCEPTION
#endif

#if defined(WITH_CPYCHECKER_TYPE_OBJECT_FOR_TYPEDEF_ATTRIBUTE)
  #define CPYCHECKER_TYPE_OBJECT_FOR_TYPEDEF(typename) \
     __attribute__((cpychecker_type_object_for_typedef(typename)))
#else
  #define CPYCHECKER_TYPE_OBJECT_FOR_TYPEDEF(typename)
#endif

#endif /* WITH_CPYCHECKER_H */
