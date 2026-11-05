// SPDX-License-Identifier: GPL-3.0-only
// Included inside mounted_gun_runtime_detail; independently exercised by the
// bridge fixture. Stack word offsets refer to pushad + pushfd input images.

__declspec(naked) void __cdecl invoke_aim(std::uint32_t*,void*) {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        mov eax,esp
        push ebp
        push eax
        push dword ptr [ebp+44]
        push dword ptr [ebp+40]
        mov esi,[esp]

        sub esp,36
        mov edi,esp
        mov ecx,9
        cld
        rep movsd
        mov eax,[esp+40]
        fxrstor [eax]
        popad
        popfd
        call dword ptr [aim_original]

        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        mov esi,ebp
        mov edi,[ebp+36]
        mov ecx,9
        rep movsd
        mov esi,esp
        mov edi,[ebp+40]
        mov ecx,128
        rep movsd
        mov eax,[ebp+44]
        fxrstor [eax]
        mov esp,[ebp+48]
        popad
        popfd
        ret
    }
}
__declspec(naked) void aim_bridge() {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        mov eax,esp
        push 0
        push eax
        push ebp
        call dispatch
        lea esp,[esp+12]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl invoke_fire(std::uint32_t*,void*) {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        mov eax,esp
        push ebp
        push eax
        push dword ptr [ebp+44]
        push dword ptr [ebp+40]
        mov esi,[esp]
        push dword ptr [esi+44]
        push dword ptr [esi+40]
        sub esp,36
        mov edi,esp
        mov ecx,9
        cld
        rep movsd
        mov eax,[esp+48]
        fxrstor [eax]
        popad
        popfd
        call dword ptr [fire_original]
        lea esp,[esp+8]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        mov esi,ebp
        mov edi,[ebp+36]
        mov ecx,9
        rep movsd
        mov esi,esp
        mov edi,[ebp+40]
        mov ecx,128
        rep movsd
        mov eax,[ebp+44]
        fxrstor [eax]
        mov esp,[ebp+48]
        popad
        popfd
        ret
    }
}
__declspec(naked) void fire_bridge() {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        mov eax,esp
        push 1
        push eax
        push ebp
        call dispatch
        lea esp,[esp+12]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl invoke_visual(std::uint32_t*,void*) {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        mov eax,esp
        push ebp
        push eax
        push dword ptr [ebp+44]
        push dword ptr [ebp+40]
        mov esi,[esp]
        push dword ptr [esi+40]
        sub esp,36
        mov edi,esp
        mov ecx,9
        cld
        rep movsd
        mov eax,[esp+44]
        fxrstor [eax]
        popad
        popfd
        call dword ptr [visual_original]
        lea esp,[esp+4]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        mov esi,ebp
        mov edi,[ebp+36]
        mov ecx,9
        rep movsd
        mov esi,esp
        mov edi,[ebp+40]
        mov ecx,128
        rep movsd
        mov eax,[ebp+44]
        fxrstor [eax]
        mov esp,[ebp+48]
        popad
        popfd
        ret
    }
}
__declspec(naked) void visual_bridge() {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        cld
        mov eax,esp
        push 2
        push eax
        push ebp
        call dispatch
        lea esp,[esp+12]
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret
    }
}

