#include <conditional_memcpy.hpp>

void * conditional_memcpy_eq_32(bool condition, void * dest, const void * src, std::size_t count) {
    if (count != 32) {
        __builtin_unreachable();
    }

    return conditional_memcpy(condition, dest, src, count);
}

void * conditional_memcpy_lt_32(bool condition, void * dest, const void * src, std::size_t count) {
    if (count >= 32) {
        __builtin_unreachable();
    }

    return conditional_memcpy(condition, dest, src, count);
}

void * conditional_memcpy_eq_16(bool condition, void * dest, const void * src, std::size_t count) {
    if (count != 16) {
        __builtin_unreachable();
    }

    return conditional_memcpy(condition, dest, src, count);
}

void * conditional_memcpy_lt_16(bool condition, void * dest, const void * src, std::size_t count) {
    if (count >= 16) {
        __builtin_unreachable();
    }

    return conditional_memcpy(condition, dest, src, count);
}


void * conditional_memcpy_eq_8(bool condition, void * dest, const void * src, std::size_t count) {
    if (count != 8) {
        __builtin_unreachable();
    }

    return conditional_memcpy(condition, dest, src, count);
}

void * conditional_memcpy_lt_8(bool condition, void * dest, const void * src, std::size_t count) {
    if (count >= 8) {
        __builtin_unreachable();
    }

    return conditional_memcpy(condition, dest, src, count);
}



void * conditional_memcpy_eq_4(bool condition, void * dest, const void * src, std::size_t count) {
    if (count != 4) {
        __builtin_unreachable();
    }

    return conditional_memcpy(condition, dest, src, count);
}

void * conditional_memcpy_lt_4(bool condition, void * dest, const void * src, std::size_t count) {
    if (count >= 4) {
        __builtin_unreachable();
    }

    return conditional_memcpy(condition, dest, src, count);
}


void * conditional_memcpy_eq_2(bool condition, void * dest, const void * src, std::size_t count) {
    if (count != 2) {
        __builtin_unreachable();
    }

    return conditional_memcpy(condition, dest, src, count);
}

void * conditional_memcpy_lt_2(bool condition, void * dest, const void * src, std::size_t count) {
    if (count >= 2) {
        __builtin_unreachable();
    }

    return conditional_memcpy(condition, dest, src, count);
}

void * conditional_memcpy_eq_1(bool condition, void * dest, const void * src, std::size_t count) {
    if (count != 1) {
        __builtin_unreachable();
    }

    return conditional_memcpy(condition, dest, src, count);
}

void * conditional_memcpy_any(bool condition, void * dest, const void * src, std::size_t count) {
    return conditional_memcpy(condition, dest, src, count);
}

conditional_memcpy_signature get_conditional_memcpy_function(std::size_t count) {
    if (count == 1) {
        return conditional_memcpy_eq_1;
    } else if (count == 2) {
        return conditional_memcpy_eq_2;
    } else if (count > 2 && count < 4) {
        return conditional_memcpy_lt_4;
    } else if (count == 4) {
        return conditional_memcpy_eq_4;
    } else if (count > 4 && count < 8) {
        return conditional_memcpy_lt_8;
    } else if (count == 8) {
        return conditional_memcpy_eq_8;
    } else if (count > 8 && count < 16) {
        return conditional_memcpy_lt_16;
    } else if (count == 16) {
        return conditional_memcpy_eq_16;
    } else if (count > 16 && count < 32) {
        return conditional_memcpy_lt_32;
    } else if (count == 32) {
        return conditional_memcpy_eq_32;
    } else {
        return conditional_memcpy_any;
    }
}