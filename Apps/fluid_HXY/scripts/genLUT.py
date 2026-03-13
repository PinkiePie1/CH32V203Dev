import re

NETLIST_FILE = "netlist.txt"

NET_TO_PB = {
    "LED1": 12,
    "LED2": 6,
    "LED3": 1,
    "LED4": 3,
    "LED5": 4,
    "LED6": 5,
    "LED7": 2,
    "LED8": 7,
    "LED9": 11,
    "LED10": 15,
    "LED11": 10,
    "LED12": 8,
    "LED13": 0,
    "LED14": 13,
    "LED15": 14,
    "LED16": 9,
}

# -------------------------------------------------
# 读取网表
# -------------------------------------------------

nets = {}
current_net = None

with open(NETLIST_FILE) as f:
    for line in f:

        line = line.strip()

        if not line:
            continue

        # 新节点
        if ";" in line:
            net, rest = line.split(";", 1)
            current_net = net.strip()

            nets[current_net] = []

            line = rest

        # 去掉逗号
        line = line.replace(",", "")

        tokens = line.split()

        nets[current_net].extend(tokens)


# -------------------------------------------------
# 提取LED正负极
# -------------------------------------------------

led_pos = {}
led_neg = {}

for net, pins in nets.items():
    for p in pins:

        m = re.match(r"LED(\d+)\.(\d)", p)
        if not m:
            continue

        led = int(m.group(1))
        pin = m.group(2)

        if pin == "1":
            led_neg[led] = net
        else:
            led_pos[led] = net


# -------------------------------------------------
# Charlieplex编号
# -------------------------------------------------

def charlie_index(high, low):
    if low < high:
        return high * 15 + low
    else:
        return high * 15 + (low - 1)


# -------------------------------------------------
# 生成LUT
# -------------------------------------------------

LUT = []

for i in range(1, 241):

    if i not in led_pos or i not in led_neg:
        raise RuntimeError(f"LED{i} 未找到正负极连接")

    pos_net = led_pos[i]
    neg_net = led_neg[i]

    high = NET_TO_PB[pos_net]
    low = NET_TO_PB[neg_net]

    idx = charlie_index(high, low)

    LUT.append(idx)


# -------------------------------------------------
# 输出C数组
# -------------------------------------------------

print("const uint16_t LUT[240] = {")

for i, v in enumerate(LUT):

    if i % 8 == 0:
        print("    ", end="")

    print(f"{v:3d}", end="")

    if i != 239:
        print(", ", end="")

    if i % 8 == 7:
        print()

print("\n};")
