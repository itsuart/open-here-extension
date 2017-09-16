#pragma once
#define COM_CALL(x, y, ...) x->lpVtbl->y(x, __VA_ARGS__)

#define COM_CALL0(x, y) x->lpVtbl->y(x)

#define COM_RELEASE(x) COM_CALL0(x, Release)
#define COM_ADDREF(x) COM_CALL0(x, AddRef)
