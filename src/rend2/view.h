#pragma once

#include "linalg.h"

struct View {
    m4f projection;
    m4f view;
    v3f position;
    f32 znear;
    f32 zfar;
};
