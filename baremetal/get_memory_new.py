# memory = ['00000000'] * (256//4*1024*1024)

memory = ['00000000'] * (256//4*500)


def read_memory(path):
    global memory
    with open(path, 'r') as f:
        lines = f.readlines()
    for line in lines:
        try:
            assert line[1:9].isalnum()
            addr = int(line[1:9], 16)
        except:
            continue
        addr = addr - 0x80000000
        content = [line[10+i*9:18+i*9] for i in range(4) if line[10+i*9:18+i*9].isalnum()]
        for c in content:
            memory[addr//4] = c[6:8] + c[4:6] + c[2:4] + c[0:2]
            addr += 4


def read_memory_offset(path):
    global memory
    with open(path, 'r') as f:
        lines = f.readlines()
    for line in lines:
        try:
            assert line[1:9].isalnum()
            addr = int(line[1:9], 16)
        except:
            continue
        addr = addr - 0xc0000000 + 0x400000
        content = [line[10+i*9:18+i*9] for i in range(4) if line[10+i*9:18+i*9].isalnum()]
        for c in content:
            memory[addr//4] = c[6:8] + c[4:6] + c[2:4] + c[0:2]
            addr += 4



# read_memory('./code/new/fw_jump.code')
# read_memory_offset('./code/vmlinux.new')
# read_memory('./code/vmlinux.code')
# read_memory('./code/busybox.code')

def read(path, offset):
    with open(path, 'r') as f:
        lines = f.readlines()
    for line in lines:
        if line[-1] == '\n':
            line = line[:-1]
        content = [i for i in line.split(' ') if i != '' and i != '\n']
        content += ['0000'] * (9-len(content))
        addr = content[0]
        addr = int(addr, 16) + offset
        for i in range(4):
            try:
                inst = content[2*i + 2] + content[2*i + 1]
            except:
                print(content)
                exit()
            memory[addr//4] = inst
            # print(inst)
            addr += 4
        # exit()

# read('./fw_jump2.code', 0)
# read('./linux2.code', 0x400000)
# read('./initrd3.code', 0x4400000)
# read('./tree3.code', 0x3e00000)
name = "aes256-zscrypto-O3"
read('test/' + name + '.code', 0x0)


# with open('./code/sdcard.code', 'r') as f:
#     lines = f.readlines()
# for line in lines:
#     addr = line[0:7]
#     addr = int(addr, 16)
#     for i in range(4):
#         inst = line[19+i*16:23+i*16] + line[11+i*16:15+i*16]
#         memory[addr//4] = inst
#         addr += 4

with open('build/' + name + '/memory', 'w') as f:
    for mem in memory:
        f.write(mem)
        f.write('\n')
    
import os
os.system("cp test/" + name + ".program build/" + name + "/program")
if "reference" in name:
    os.system("./a.out build/" + name + "/memory > build/" + name + "/log")
    os.system("tail -n 10 build/" + name + "/log")