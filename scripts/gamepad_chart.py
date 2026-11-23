#!/usr/bin/env python3
"""Draw the gamepad button chart shown while L1/L2/R1/PS is held.

Usage: python3 scripts/gamepad_chart.py en|uk OUT.bmp
The engine loads HighRes/gamepad.bmp from the game folder.
Needs Pillow and the macOS Georgia font.
"""
import sys
from PIL import Image, ImageDraw, ImageFont

W, H = 1600, 900
BG = (28, 22, 16)
FRAME = (150, 118, 70)
TEXT = (232, 220, 196)
DIM = (170, 150, 120)
TITLE = (214, 178, 110)

FONT_DIR = '/System/Library/Fonts/Supplemental/'

TEXTS = {
    'en': {
        'title': 'Gamepad controls',
        'note': 'Hold L1, L2, R1 or PS to see this chart',
        'left': [
            ('dpad:up', 'Character'),
            ('dpad:right', 'World map'),
            ('dpad:down', 'Schematics'),
            ('dpad:left', 'Inventory'),
            ('tag:L2+dpad', 'Hotkeys 1–4, dialogue replies'),
            ('tag:L1+dpad', 'Hotkeys 5–8'),
            ('tag:R1+dpadv', 'Hotkeys 9 (up) / 0 (down)'),
            ('tag:L1', 'Tap: skills'),
            ('tag:R1', 'Tap: spells'),
            ('tag:R2', 'Combat mode'),
        ],
        'right': [
            ('btn:cross', 'Left click (touchpad too)'),
            ('btn:circle', 'Map: right click; windows: back'),
            ('btn:square', 'Map: highlight; windows: right click'),
            ('btn:triangle', 'Turn-based mode / end turn'),
            ('tag:L stick', 'Cursor; hold pressed: precise'),
            ('tag:R stick', 'Look around; press: back to hero'),
            ('tag:Options', 'Game menu'),
            ('tag:Create', 'Quick save; hold: quick load'),
        ],
    },
    'uk': {
        'title': 'Керування геймпадом',
        'note': 'Утримуйте L1, L2, R1 або PS, щоб побачити цю схему',
        'left': [
            ('dpad:up', 'Персонаж'),
            ('dpad:right', 'Мапа світу'),
            ('dpad:down', 'Схеми'),
            ('dpad:left', 'Інвентар'),
            ('tag:L2+dpad', 'Слоти 1–4, репліки в розмові'),
            ('tag:L1+dpad', 'Слоти 5–8'),
            ('tag:R1+dpadv', 'Слоти 9 (вгору) / 0 (вниз)'),
            ('tag:L1', 'Натиснути: навички'),
            ('tag:R1', 'Натиснути: закляття'),
            ('tag:R2', 'Бойовий режим'),
        ],
        'right': [
            ('btn:cross', 'Лівий клік (і тачпад)'),
            ('btn:circle', 'Мапа: правий клік; вікна: назад'),
            ('btn:square', 'Мапа: підсвітка; вікна: правий клік'),
            ('btn:triangle', 'Покроковий бій / кінець ходу'),
            ('tag:Лівий стік', 'Курсор; затиснути — точно'),
            ('tag:Правий стік', 'Огляд мапи; натиснути — до героя'),
            ('tag:Options', 'Меню гри'),
            ('tag:Create', 'Швидке збереження; тримати — завантажити'),
        ],
    },
}

BTN_COLORS = {
    'cross': (110, 150, 230),
    'circle': (230, 95, 95),
    'square': (220, 130, 200),
    'triangle': (90, 200, 160),
}


def font(size, bold=False):
    # Baskerville has lining figures (Georgia's old-style 1 reads as "I").
    return ImageFont.truetype(FONT_DIR + 'Baskerville.ttc', size, index=1 if bold else 0)


def draw_button(d, cx, cy, kind):
    r = 24
    d.ellipse((cx - r, cy - r, cx + r, cy + r), fill=(52, 44, 36), outline=(110, 95, 75), width=2)
    c = BTN_COLORS[kind]
    s = 11
    if kind == 'cross':
        d.line((cx - s, cy - s, cx + s, cy + s), fill=c, width=4)
        d.line((cx - s, cy + s, cx + s, cy - s), fill=c, width=4)
    elif kind == 'circle':
        d.ellipse((cx - s, cy - s, cx + s, cy + s), outline=c, width=4)
    elif kind == 'square':
        d.rectangle((cx - s + 1, cy - s + 1, cx + s - 1, cy + s - 1), outline=c, width=4)
    else:
        d.polygon((cx, cy - s - 1, cx + s + 2, cy + s - 3, cx - s - 2, cy + s - 3), outline=c, width=4)
    return cx + r


def draw_dpad(d, cx, cy, arm):
    a, w = 22, 9
    base = (70, 60, 50)
    hi = TITLE
    d.rectangle((cx - w, cy - a, cx + w, cy + a), fill=base)
    d.rectangle((cx - a, cy - w, cx + a, cy + w), fill=base)
    if arm == 'updown':
        d.rectangle((cx - w, cy - a, cx + w, cy - w), fill=hi)
        d.rectangle((cx - w, cy + w, cx + w, cy + a), fill=hi)
    elif arm == 'up':
        d.rectangle((cx - w, cy - a, cx + w, cy - w), fill=hi)
    elif arm == 'down':
        d.rectangle((cx - w, cy + w, cx + w, cy + a), fill=hi)
    elif arm == 'left':
        d.rectangle((cx - a, cy - w, cx - w, cy + w), fill=hi)
    elif arm == 'right':
        d.rectangle((cx + w, cy - w, cx + a, cy + w), fill=hi)
    return cx + a


def draw_tag(d, x, cy, label, f):
    parts = label.split('+')
    for i, part in enumerate(parts):
        if part in ('dpad', 'dpadv'):
            x = draw_dpad(d, x + 22, cy, 'updown' if part == 'dpadv' else None) + 4
        else:
            tw = d.textlength(part, font=f)
            d.rounded_rectangle((x, cy - 20, x + tw + 22, cy + 20), radius=10,
                                fill=(52, 44, 36), outline=(110, 95, 75), width=2)
            d.text((x + 11, cy), part, font=f, fill=TEXT, anchor='lm')
            x += tw + 22
        if i + 1 < len(parts):
            d.text((x + 8, cy), '+', font=f, fill=DIM, anchor='lm')
            x += 28
    return x


def draw_row(d, x, cy, key, text, ftag, ftext, key_w):
    kind, _, arg = key.partition(':')
    if kind == 'btn':
        draw_button(d, x + 24, cy, arg)
    elif kind == 'dpad':
        draw_dpad(d, x + 24, cy, arg)
    else:
        draw_tag(d, x, cy, arg, ftag)
    d.text((x + key_w, cy), text, font=ftext, fill=TEXT, anchor='lm')


def main():
    lang, out = sys.argv[1], sys.argv[2]
    t = TEXTS[lang]
    img = Image.new('RGB', (W, H), BG)
    d = ImageDraw.Draw(img)
    d.rectangle((8, 8, W - 9, H - 9), outline=FRAME, width=3)
    d.rectangle((18, 18, W - 19, H - 19), outline=(90, 72, 48), width=1)
    d.text((W // 2, 70), t['title'], font=font(50, True), fill=TITLE, anchor='mm')
    d.line((W // 2 - 300, 112, W // 2 + 300, 112), fill=FRAME, width=2)

    ftag, ftext = font(25), font(30)
    top, step = 175, 64
    for i, (k, s) in enumerate(t['left']):
        draw_row(d, 60, top + i * step, k, s, ftag, ftext, 190)
    for i, (k, s) in enumerate(t['right']):
        draw_row(d, 770, top + i * step, k, s, ftag, ftext, 185)

    d.text((W // 2, H - 45), t['note'], font=font(22), fill=DIM, anchor='mm')
    img.save(out)


if __name__ == '__main__':
    main()
