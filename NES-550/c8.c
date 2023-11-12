#include <SDL2/SDL.h>
#include <stdint.h>
#define PULL mem(++S, 1, 0, 0)
#define PUSH(x) mem(S--, 1, x, 1);
#define OP16(x) \
    break;      \
    case x:     \
    case x + 16:
int rgba[64] = {25356, 34816, 39011, 30854, 24714, 4107, 106, 2311, 2468,
                2561, 4642, 6592, 20832, 0, 0, 0, 44373, 49761,
                55593, 51341, 43186, 18675, 434, 654, 4939, 5058, 3074,
                19362, 37667, 0, 0, 0, ~0, ~819, 64497, 64342,
                62331, 43932, 23612, 9465, 1429, 1550, 20075, 36358, 52713,
                16904, 0, 0, ~0, ~328, ~422, ~452, ~482, 58911,
                50814, 42620, 40667, 40729, 48951, 53078, 61238, 44405},
    scany,
    shift_at;
uint8_t *rom, *chrrom,
    prg[2], chr[2],
    A, X, Y, P = 4, S = ~2, PCH, PCL,
             addr_lo, addr_hi,
             nomem,
             result,
             val,
             cross,
             tmp, tmp2,
             ppumask, ppuctrl, ppustatus,
             ppubuf,
             W,
             fine_x,
             opcode,
             nmi,
             ntb,
             ptb_lo, ptb_hi,
             vram[2048],
             palette_ram[64],
             ram[8192],
             chrram[8192],
             prgram[8192],
             oam[256],
             mask[] = {128, 64, 1, 2,
                       1, 0, 0, 1, 4, 0, 0, 4, 0,
                       0, 64, 0, 8, 0, 0, 8},
             keys,
             mirror,
             mmc1_bits, mmc1_data, mmc1_ctrl,
             chrbank0, chrbank1, prgbank,
             rombuf[1024 * 1024],
             *key_state;
uint16_t T, V,
    sum,
    dot,
    atb,
    shift_hi, shift_lo,
    cycles,
    frame_buffer[61440];
uint8_t *get_chr_byte(uint16_t a)
{
    return &chrrom[chr[a >> 12] << 12 | a & 4095];
}
uint8_t *get_nametable_byte(uint16_t a)
{
    return &vram[!mirror       ? a % 1024
                 : mirror == 1 ? a % 1024 + 1024
                 : mirror == 2 ? a & 2047
                               : a / 2 & 1024 | a % 1024];
}
uint8_t mem(uint8_t lo, uint8_t hi, uint8_t val, uint8_t write)
{
    uint16_t a = hi << 8 | lo;
    switch (hi >> 4)
    {
    case 0 ... 1:
        return write ? ram[a] = val : ram[a];
    case 2 ... 3:
        lo &= 7;
        if (lo == 7)
        {
            tmp = ppubuf;
            uint8_t *rom =

                V < 8192 ? !write || chrrom == chrram ? get_chr_byte(V) : &tmp2

                : V < 16128 ? get_nametable_byte(V)

                            : palette_ram + (uint8_t)((V & 19) == 16 ? V ^ 16 : V);
            write ? *rom = val : (ppubuf = *rom);
            V += ppuctrl & 4 ? 32 : 1;
            V %= 16384;
            return tmp;
        }
        if (write)
            switch (lo)
            {
            case 0:
                ppuctrl = val;
                T = T & 62463 | val % 4 << 10;
                break;
            case 1:
                ppumask = val;
                break;
            case 5:
                T = (W ^= 1)      ? fine_x = val & 7,
                T & ~31 | val / 8 : T & 35871 | val % 8 << 12 | (val & 248) * 4;
                break;

            case 6:
                T = (W ^= 1) ? T & 255 | val % 64 << 8 : (V = T & ~255 | val);
            }
        if (lo == 2)
            return tmp = ppustatus & 224, ppustatus &= 127, W = 0, tmp;
        break;
    case 4:
        if (write && lo == 20)
            for (sum = 256; sum--;)
                oam[sum] = mem(sum, val, 0, 0);
        return (lo == 22) ? write ? keys = (key_state[SDL_SCANCODE_RIGHT] * 8 +
                                            key_state[SDL_SCANCODE_LEFT] * 4 +
                                            key_state[SDL_SCANCODE_DOWN] * 2 +
                                            key_state[SDL_SCANCODE_UP]) *
                                               16 +
                                           key_state[SDL_SCANCODE_RETURN] * 8 +
                                           key_state[SDL_SCANCODE_TAB] * 4 +
                                           key_state[SDL_SCANCODE_Z] * 2 +
                                           key_state[SDL_SCANCODE_X]
                                  : (tmp = keys & 1, keys /= 2, tmp)
                          : 0;
    case 6 ... 7:
        return write ? prgram[a & 8191] = val : prgram[a & 8191];
    case 8 ... 15:
        if (write)
            switch (rombuf[6] >> 4)
            {
            case 7:
                mirror = !(val / 16);
                *prg = val = val % 8 * 2;
                prg[1] = val + 1;
                break;
            case 3:
                *chr = val = val % 4 * 2;
                chr[1] = val + 1;
                break;
            case 2:
                *prg = val & 31;
                break;
            case 1:
                if (val & 128)
                {
                    mmc1_bits = 5, mmc1_data = 0, mmc1_ctrl |= 12;
                }
                else if (mmc1_data = mmc1_data / 2 | val << 4 & 16, !--mmc1_bits)
                {
                    mmc1_bits = 5, tmp = a >> 13;
                    *(tmp == 4 ? mirror = mmc1_data & 3, &mmc1_ctrl
                  : tmp == 5   ? &chrbank0
                  : tmp == 6   ? &chrbank1
                               : &prgbank) = mmc1_data;
                    *chr = chrbank0 & ~!(mmc1_ctrl & 16);
                    chr[1] = mmc1_ctrl & 16 ? chrbank1 : chrbank0 | 1;
                    tmp = mmc1_ctrl / 4 & 3;
                    *prg = tmp == 2 ? 0 : tmp == 3 ? prgbank
                                                   : prgbank & ~1;
                    prg[1] = tmp == 2 ? prgbank : tmp == 3 ? rombuf[4] - 1
                                                           : prgbank | 1;
                }
            }
        return rom[prg[(a >> 14) - 2] << 14 | a & 16383];
    }
    return ~0;
}
uint8_t read_pc()
{
    val = mem(PCL, PCH, 0, 0);
    !++PCL ? ++PCH : 0;
    return val;
}
uint8_t set_nz(uint8_t val) { return P = P & ~130 | val & 128 | !val * 2; }
int main(int argc, char **argv)
{
    SDL_RWread(SDL_RWFromFile(argv[1], "rb"), rombuf, 1024 * 1024, 1);
    rom = rombuf + 16;
    prg[1] = rombuf[4] - 1;
    chrrom = rombuf[5] ? rom + ((prg[1] + 1) << 14) : chrram;
    chr[1] = (rombuf[5] ? rombuf[5] : 1) * 2 - 1;
    mirror = !(rombuf[6] & 1) + 2;
    PCL = mem(~3, ~0, 0, 0);
    PCH = mem(~2, ~0, 0, 0);
    SDL_Init(SDL_INIT_VIDEO);
    void *renderer = SDL_CreateRenderer(
        SDL_CreateWindow("C8-550", 0, 0, 1024, 840, SDL_WINDOW_SHOWN), -1,
        SDL_RENDERER_PRESENTVSYNC);
    void *texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_BGR565,
                                      SDL_TEXTUREACCESS_STREAMING, 256, 224);
    key_state = (uint8_t *)SDL_GetKeyboardState(0);
    for (;;)
    {
        cycles = nomem = 0;
        if (nmi)
            goto nmi;
        switch ((opcode = read_pc()) & 31)
        {
        case 0:
            if (opcode & 128)
            {
                read_pc();
                nomem = 1;
                goto nomemop;
            }
            switch (opcode >> 5)
            {
            case 0:
                !++PCL ? ++PCH : 0;
            nmi:
                PUSH(PCH)
                PUSH(PCL)
                PUSH(P | 32)
                PCL = mem(~1 - nmi * 4, ~0, 0, 0);
                PCH = mem(~0 - nmi * 4, ~0, 0, 0);
                cycles++;
                nmi = 0;
                break;
            case 1:
                result = read_pc();
                PUSH(PCH)
                PUSH(PCL)
                PCH = read_pc();
                PCL = result;
                break;
            case 2:
                P = PULL & ~32;
                PCL = PULL;
                PCH = PULL;
                break;
            case 3:
                PCL = PULL;
                PCH = PULL;
                !++PCL ? ++PCH : 0;
                break;
            }
            cycles += 4;
            break;
        case 16:
            read_pc();
            if (!(P & mask[opcode >> 6 & 3]) ^ opcode / 32 & 1)
            {
                if (cross = PCL + (int8_t)val >> 8)
                    PCH += cross, cycles++;
                cycles++, PCL += (int)val;
            }
            OP16(8)
            switch (opcode >>= 4)
            {
            case 0:
                PUSH(P | 48)
                cycles++;
                break;
            case 2:
                P = PULL & ~16;
                cycles += 2;
                break;
            case 4:
                PUSH(A)
                cycles++;
                break;
            case 6:
                set_nz(A = PULL);
                cycles += 2;
                break;
            case 8:
                set_nz(--Y);
                break;
            case 9:
                set_nz(A = Y);
                break;
            case 10:
                set_nz(Y = A);
                break;
            case 12:
                set_nz(++Y);
                break;
            case 14:
                set_nz(++X);
                break;
            default:
                P = P & ~mask[opcode + 3] | mask[opcode + 4];
                break;
            }
            OP16(10)
            switch (opcode >> 4)
            {
            case 8:
                set_nz(A = X);
                break;
            case 9:
                S = X;
                break;
            case 10:
                set_nz(X = A);
                break;
            case 11:
                set_nz(X = S);
                break;
            case 12:
                set_nz(--X);
                break;
            case 14:
                break;
            default:
                nomem = 1;
                val = A;
                goto nomemop;
            }
            break;
        case 1:
            read_pc();
            val += X;
            addr_lo = mem(val, 0, 0, 0);
            addr_hi = mem(val + 1, 0, 0, 0);
            cycles += 4;
            goto opcode;
        case 4 ... 6:
            addr_lo = read_pc();
            addr_hi = 0;
            cycles++;
            goto opcode;
        case 2:
        case 9:
            read_pc();
            nomem = 1;
            goto nomemop;
        case 12 ... 14:
            addr_lo = read_pc();
            addr_hi = read_pc();
            cycles += 2;
            goto opcode;
        case 17:
            addr_lo = mem(read_pc(), 0, 0, 0);
            addr_hi = mem(val + 1, 0, 0, 0);
            val = Y;
            tmp = opcode == 145;
            cycles++;
            goto cross;
        case 20 ... 22:
            addr_lo = read_pc() + ((opcode & 214) == 150 ? Y : X);
            addr_hi = 0;
            cycles += 2;
            goto opcode;
        case 25:
            addr_lo = read_pc();
            addr_hi = read_pc();
            val = Y;
            tmp = opcode == 153;
            goto cross;
        case 28 ... 30:
            addr_lo = read_pc();
            addr_hi = read_pc();
            val = opcode == 190 ? Y : X;
            tmp = opcode == 157 ||
                  opcode % 16 == 14 && opcode != 190;
        cross:
            addr_hi += cross = addr_lo + val > 255;
            addr_lo += val;
            cycles += 2 + tmp | cross;
        opcode:
            (opcode & 224) != 128 &&opcode != 76 ? val = mem(addr_lo, addr_hi, 0, 0)
                                                 : 0;
        nomemop:
            switch (opcode & 243)
            {
                OP16(1)
                set_nz(A |= val);
                OP16(33)
                set_nz(A &= val);
                OP16(65)
                set_nz(A ^= val);
                OP16(225)
                val = ~val;
                goto add;
                OP16(97)
            add:
                sum = A + val + (P & 1);
                P = P & ~65 | sum > 255 | (~(A ^ val) & (val ^ sum) & 128) / 2;
                set_nz(A = sum);
                OP16(2)
                result = val * 2;
                P = P & ~1 | val / 128;
                goto memop;
                OP16(34)
                result = val * 2 | P & 1;
                P = P & ~1 | val / 128;
                goto memop;
                OP16(66)
                result = val / 2;
                P = P & ~1 | val & 1;
                goto memop;
                OP16(98)
                result = val / 2 | P << 7;
                P = P & ~1 | val & 1;
                goto memop;
                OP16(194)
                result = val - 1;
                goto memop;
                OP16(226)
                result = val + 1;
            memop:
                set_nz(result);
                nomem ? A = result : (cycles += 2, mem(addr_lo, addr_hi, result, 1));
                break;
            case 32:
                P = P & 61 | val & 192 | !(A & val) * 2;
                break;
            case 64:
                PCL = addr_lo;
                PCH = addr_hi;
                cycles--;
                break;
            case 96:
                PCL = val;
                PCH = mem(addr_lo + 1, addr_hi, 0, 0);
                cycles++;
                OP16(160)
                set_nz(Y = val);
                OP16(161)
                set_nz(A = val);
                OP16(162)
                set_nz(X = val);
                OP16(128)
                result = Y;
                goto store;
                OP16(129)
                result = A;
                goto store;
                OP16(130)
                result = X;
            store:
                mem(addr_lo, addr_hi, result, 1);
                OP16(192)
                result = Y;
                goto cmp;
                OP16(193)
                result = A;
                goto cmp;
                OP16(224)
                result = X;
            cmp:
                P = P & ~1 | result >= val;
                set_nz(result - val);
                break;
            }
        }
        for (tmp = cycles * 3 + 6; tmp--;)
        {
            if (ppumask & 24)
            {
                if (scany < 240)
                {
                    if (dot < 256 || dot > 319)
                    {
                        switch (dot & 7)
                        {
                        case 1:
                            ntb = *get_nametable_byte(V);
                            break;
                        case 3:
                            atb = (*get_nametable_byte(960 | V & 3072 | V >> 4 & 56 |
                                                       V / 4 & 7) >>
                                   (V >> 5 & 2 | V / 2 & 1) * 2) &
                                  3;
                            atb |= atb * 4;
                            atb |= atb << 4;
                            atb |= atb << 8;
                            break;
                        case 5:
                            ptb_lo = *get_chr_byte(ppuctrl << 8 & 4096 | ntb << 4 | V >> 12);
                            break;
                        case 7:
                            ptb_hi =
                                *get_chr_byte(ppuctrl << 8 & 4096 | ntb << 4 | V >> 12 | 8);

                            V = (V & 31) == 31 ? V & ~31 ^ 1024 : V + 1;
                            break;
                        }
                        if ((uint16_t)scany < 240 && dot < 256)
                        {
                            uint8_t color = shift_hi >> 14 - fine_x & 2 |
                                            shift_lo >> 15 - fine_x & 1,
                                    palette = shift_at >> 28 - fine_x * 2 & 12;
                            if (ppumask & 16)
                                for (uint8_t *sprite = oam; sprite < oam + 256; sprite += 4)
                                {
                                    uint16_t sprite_h = ppuctrl & 32 ? 16 : 8,
                                             sprite_x = dot - sprite[3],
                                             sprite_y = scany - *sprite - 1,
                                             sx = sprite_x ^ (sprite[2] & 64 ? 0 : 7),
                                             sy = sprite_y ^ (sprite[2] & 128 ? sprite_h - 1 : 0);
                                    if (sprite_x < 8 && sprite_y < sprite_h)
                                    {
                                        uint16_t sprite_tile = sprite[1],
                                                 sprite_addr = ppuctrl & 32
                                                                   ? sprite_tile % 2 << 12 |
                                                                         (sprite_tile & ~1) << 4 |
                                                                         (sy & 8) * 2 | sy & 7
                                                                   : (ppuctrl & 8) << 9 |
                                                                         sprite_tile << 4 | sy & 7,
                                                 sprite_color =
                                                     *get_chr_byte(sprite_addr + 8) >> sx << 1 & 2 |
                                                     *get_chr_byte(sprite_addr) >> sx & 1;
                                        if (sprite_color)
                                        {
                                            !(sprite[2] & 32 && color)
                                                ? color = sprite_color,
                                                  palette = 16 | sprite[2] * 4 & 12 : 0;
                                            sprite == oam &&color ? ppustatus |= 64 : 0;
                                            break;
                                        }
                                    }
                                }
                            frame_buffer[scany * 256 + dot] =
                                rgba[palette_ram[color ? palette | color : 0]];
                        }
                        dot < 336 ? shift_hi *= 2, shift_lo *= 2, shift_at *= 4 : 0;
                        dot % 8 == 7        ? shift_hi |= ptb_hi, shift_lo |= ptb_lo,
                            shift_at |= atb : 0;
                    }
                    dot == 256 ? V = ((V & 7 << 12) != 7 << 12 ? V + 4096
                                      : (V & 992) == 928       ? V & 35871 ^ 2048
                                      : (V & 992) == 992       ? V & 35871
                                                               : V & 35871 | V + 32 & 992) &
                                         ~1055 |
                                     T & 1055
                               : 0;
                }
                scany == -1 &&dot > 279 &&dot < 305 ? V = V & 33823 | T & 31712 : 0;
            }
            if (scany == 241 && dot == 1)
            {
                ppuctrl & 128 ? nmi = 1 : 0;
                ppustatus |= 128;
                SDL_UpdateTexture(texture, 0, frame_buffer + 2048, 512);
                SDL_RenderCopy(renderer, texture, 0, 0);
                SDL_RenderPresent(renderer);
                for (SDL_Event event; SDL_PollEvent(&event);)
                    if (event.type == SDL_QUIT)
                        return 0;
            }
            scany == -1 &&dot == 1 ? ppustatus = 0 : 0;
            ++dot == 341 ? dot = 0, scany = scany == 260 ? -1 : scany + 1 : 0;
        }
    }
}
