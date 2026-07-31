import os
import glob
import random

def encrypt_file(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()
    
    if data.startswith(b'GNWENC\x01'):
        print("Already encrypted: " + filepath)
        return
        
    salt = bytearray([random.randint(0, 255) for _ in range(8)])
    key = bytearray(b'GnW!nf0_S3cr3t_2026')
    
    content = bytearray(data)
    for i in range(len(content)):
        content[i] ^= key[i % len(key)] ^ salt[i % 8]
        
    out_data = b'GNWENC\x01' + salt + content
    with open(filepath, 'wb') as f:
        f.write(out_data)
    print("Encrypted: " + filepath)

def decrypt_file(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()
        
    if not data.startswith(b'GNWENC\x01'):
        print("Not encrypted: " + filepath)
        return
        
    salt = bytearray(data[7:15])
    content = bytearray(data[15:])
    key = bytearray(b'GnW!nf0_S3cr3t_2026')
    
    for i in range(len(content)):
        content[i] ^= key[i % len(key)] ^ salt[i % 8]
        
    with open(filepath, 'wb') as f:
        f.write(content)
    print("Decrypted: " + filepath)

if __name__ == "__main__":
    import sys
    action = sys.argv[1] if len(sys.argv) > 1 else 'encrypt'
    
    for root, dirs, files in os.walk('x64/Release/ICP-Optimizer'):
        for file in files:
            if file.endswith('.ps1') or file.endswith('.psm1') or file.endswith('.psd1'):
                path = os.path.join(root, file)
                if action == 'encrypt':
                    encrypt_file(path)
                elif action == 'decrypt':
                    decrypt_file(path)
