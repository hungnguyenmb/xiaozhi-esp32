#ifndef CONTROL_CAR_ROLE_H
#define CONTROL_CAR_ROLE_H

namespace control_car {

inline constexpr const char* RoleName() {
#if CONFIG_CONTROL_CAR_ROLE_PARENT
    return "PARENT";
#elif CONFIG_CONTROL_CAR_ROLE_CHILD
    return "CHILD";
#else
    return "";
#endif
}

inline constexpr bool HasRole() {
#if CONFIG_CONTROL_CAR_ROLE_PARENT || CONFIG_CONTROL_CAR_ROLE_CHILD
    return true;
#else
    return false;
#endif
}

inline constexpr bool IsParent() {
#if CONFIG_CONTROL_CAR_ROLE_PARENT
    return true;
#else
    return false;
#endif
}

inline constexpr bool IsChild() {
#if CONFIG_CONTROL_CAR_ROLE_CHILD
    return true;
#else
    return false;
#endif
}

} // namespace control_car

#endif // CONTROL_CAR_ROLE_H
