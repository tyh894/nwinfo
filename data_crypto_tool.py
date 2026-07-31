import os
import sys

def decrypt_file(filepath):
    if not os.path.exists(filepath):
        print(f"?????: {filepath}")
        return
        
    with open(filepath, 'rb') as f:
        data = f.read()
        
    if not data.startswith(b'GNWENC\x01'):
        print(f"?? (?????????): {filepath}")
        return
        
    salt = bytearray(data[7:15])
    content = bytearray(data[15:])
    key = bytearray(b'GnW!nf0_S3cr3t_2026')
    
    for i in range(len(content)):
        content[i] ^= key[i % len(key)] ^ salt[i % 8]
        
    # ???????????????????????
    dir_name = os.path.dirname(filepath)
    base_name = os.path.basename(filepath)
    name, ext = os.path.splitext(base_name)
    out_path = os.path.join(dir_name, f"{name}_??{ext}")
    
    with open(out_path, 'wb') as f:
        f.write(content)
    print(f"? ????: {base_name} -> {os.path.basename(out_path)}")

def encrypt_file(filepath):
    if not os.path.exists(filepath):
        print(f"?????: {filepath}")
        return
        
    with open(filepath, 'rb') as f:
        data = f.read()
        
    if data.startswith(b'GNWENC\x01'):
        print(f"?? (??????): {filepath}")
        return
        
    import random
    salt = bytearray([random.randint(0, 255) for _ in range(8)])
    key = bytearray(b'GnW!nf0_S3cr3t_2026')
    
    content = bytearray(data)
    for i in range(len(content)):
        content[i] ^= key[i % len(key)] ^ salt[i % 8]
        
    out_data = b'GNWENC\x01' + salt + content
    
    # ?????
    with open(filepath, 'wb') as f:
        f.write(out_data)
    print(f"?? ????: {os.path.basename(filepath)}")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("================ ????????? ================")
        print("?? 1: python data_crypto_tool.py <??????????>")
        print("        (???????????? '_??' ??????)")
        print("?? 2: python data_crypto_tool.py encrypt <??????????>")
        print("        (?????????)")
        sys.exit(1)
        
    action = 'decrypt'
    target = sys.argv[1]
    
    if len(sys.argv) >= 3:
        if sys.argv[1].lower() == 'encrypt':
            action = 'encrypt'
            target = sys.argv[2]
        elif sys.argv[1].lower() == 'decrypt':
            action = 'decrypt'
            target = sys.argv[2]
            
    if os.path.isfile(target):
        if action == 'encrypt':
            encrypt_file(target)
        else:
            decrypt_file(target)
    elif os.path.isdir(target):
        print(f"???????: {target}")
        for root, dirs, files in os.walk(target):
            for file in files:
                # ??? json ? csv ??
                if file.endswith('.json') or file.endswith('.csv'):
                    if '_??' in file:
                        continue
                    path = os.path.join(root, file)
                    if action == 'encrypt':
                        encrypt_file(path)
                    else:
                        decrypt_file(path)
    else:
        print("????????????")
