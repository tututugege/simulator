memory = ['00000000'] * (256//4*10000)

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
import os
import sys
os.system("hexdump -xv " + sys.argv[1] + " > ./test.code")

read('test.code', 0x0)

with open('memory', 'w') as f:
    for mem in memory:
        f.write(mem)
        f.write('\n')
    
