#ifndef AUTOSAR_TYPES_H
#define AUTOSAR_TYPES_H

#include <stdbool.h>
#include <stdint.h>

/* Small, project-owned subset of AUTOSAR-style basic types.  It intentionally
 * avoids claiming compatibility with a commercial AUTOSAR BSW package. */
typedef uint8_t  Autosar_BooleanType;
typedef uint8_t  Autosar_ReturnType;
typedef uint32_t Autosar_TimeMsType;

#define AUTOSAR_TRUE       ((Autosar_BooleanType) 1U)
#define AUTOSAR_FALSE      ((Autosar_BooleanType) 0U)
#define AUTOSAR_E_OK       ((Autosar_ReturnType) 0U)
#define AUTOSAR_E_NOT_OK   ((Autosar_ReturnType) 1U)

static inline Autosar_BooleanType autosar_bool(bool value)
{
    return value ? AUTOSAR_TRUE : AUTOSAR_FALSE;
}

#endif /* AUTOSAR_TYPES_H */
