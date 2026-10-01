#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
安全加密模块
提供多种加密方案用于保护游戏数据
"""

import os
import struct
import hashlib
import hmac
import secrets
from typing import Dict, Any, Optional, Tuple
from enum import IntEnum

# 尝试导入加密库
try:
    from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
    from cryptography.hazmat.primitives.kdf.pbkdf2 import PBKDF2HMAC
    from cryptography.hazmat.primitives import hashes
    from cryptography.hazmat.backends import default_backend
    HAS_CRYPTOGRAPHY = True
except ImportError:
    HAS_CRYPTOGRAPHY = False

try:
    import pyaes
    HAS_PYAES = True
except ImportError:
    HAS_PYAES = False


class EncryptionType(IntEnum):
    """加密类型枚举"""
    NONE = 0          # 无加密
    XOR = 1           # 简单XOR加密（不安全，仅用于兼容）
    AES256_GCM = 2    # AES-256-GCM
    CHACHA20_POLY1305 = 3  # ChaCha20-Poly1305
    CUSTOM = 4        # 自定义加密方案


class SecureEncryptor:
    """安全加密器"""

    def __init__(self, encryption_type: EncryptionType = EncryptionType.AES256_GCM):
        self.encryption_type = encryption_type
        self.backend = default_backend() if HAS_CRYPTOGRAPHY else None

    def derive_key(self, password: str, salt: bytes, key_length: int = 32, iterations: int = 100000) -> bytes:
        """使用PBKDF2从密码派生密钥"""
        if not HAS_CRYPTOGRAPHY:
            # 降级方案：使用简单的hash派生
            return hashlib.pbkdf2_hmac('sha256', password.encode('utf-8'), salt, iterations, key_length)

        kdf = PBKDF2HMAC(
            algorithm=hashes.SHA256(),
            length=key_length,
            salt=salt,
            iterations=iterations,
            backend=self.backend
        )
        return kdf.derive(password.encode('utf-8'))

    def encrypt_aes256_gcm(self, data: bytes, key: bytes) -> bytes:
        """AES-256-GCM加密"""
        if not HAS_CRYPTOGRAPHY:
            raise ImportError("cryptography库未安装，无法使用AES-256-GCM")

        if len(key) != 32:
            raise ValueError("AES-256-GCM需要32字节密钥")

        # 生成随机nonce (12字节推荐用于GCM)
        nonce = os.urandom(12)

        # 创建加密器
        cipher = Cipher(
            algorithms.AES(key),
            modes.GCM(nonce),
            backend=self.backend
        )
        encryptor = cipher.encryptor()

        # 加密数据
        ciphertext = encryptor.update(data) + encryptor.finalize()

        # 返回: nonce + ciphertext + auth_tag
        return nonce + ciphertext + encryptor.tag

    def decrypt_aes256_gcm(self, encrypted_data: bytes, key: bytes) -> bytes:
        """AES-256-GCM解密"""
        if not HAS_CRYPTOGRAPHY:
            raise ImportError("cryptography库未安装，无法使用AES-256-GCM")

        if len(key) != 32:
            raise ValueError("AES-256-GCM需要32字节密钥")

        # 提取nonce, ciphertext, auth_tag
        nonce = encrypted_data[:12]
        auth_tag = encrypted_data[-16:]
        ciphertext = encrypted_data[12:-16]

        # 创建解密器
        cipher = Cipher(
            algorithms.AES(key),
            modes.GCM(nonce, auth_tag),
            backend=self.backend
        )
        decryptor = cipher.decryptor()

        # 解密数据
        return decryptor.update(ciphertext) + decryptor.finalize()

    def encrypt_chacha20_poly1305(self, data: bytes, key: bytes) -> bytes:
        """ChaCha20-Poly1305加密"""
        if not HAS_CRYPTOGRAPHY:
            raise ImportError("cryptography库未安装，无法使用ChaCha20-Poly1305")

        if len(key) != 32:
            raise ValueError("ChaCha20-Poly1305需要32字节密钥")

        # 生成随机nonce (12字节 for ChaCha20)
        nonce = os.urandom(12)

        # 创建加密器
        cipher = Cipher(
            algorithms.ChaCha20(key, nonce),
            mode=None,
            backend=self.backend
        )
        encryptor = cipher.encryptor()

        # 加密数据
        ciphertext = encryptor.update(data) + encryptor.finalize()

        # 计算认证标签（使用HMAC-SHA256作为简化方案）
        mac_key = hashlib.sha256(key + nonce).digest()
        mac = hmac.new(mac_key, nonce + ciphertext, hashlib.sha256).digest()[:16]

        # 返回: nonce + ciphertext + auth_tag
        return nonce + ciphertext + mac

    def decrypt_chacha20_poly1305(self, encrypted_data: bytes, key: bytes) -> bytes:
        """ChaCha20-Poly1305解密"""
        if not HAS_CRYPTOGRAPHY:
            raise ImportError("cryptography库未安装，无法使用ChaCha20-Poly1305")

        if len(key) != 32:
            raise ValueError("ChaCha20-Poly1305需要32字节密钥")

        # 提取nonce, ciphertext, auth_tag
        nonce = encrypted_data[:12]
        auth_tag = encrypted_data[-16:]
        ciphertext = encrypted_data[12:-16]

        # 验证认证标签
        mac_key = hashlib.sha256(key + nonce).digest()
        mac = hmac.new(mac_key, nonce + ciphertext, hashlib.sha256).digest()[:16]

        if not hmac.compare_digest(mac, auth_tag):
            raise ValueError("认证失败：数据可能被篡改")

        # 解密数据
        cipher = Cipher(
            algorithms.ChaCha20(key, nonce),
            mode=None,
            backend=self.backend
        )
        decryptor = cipher.decryptor()
        return decryptor.update(ciphertext) + decryptor.finalize()

    def encrypt_custom(self, data: bytes, key: bytes) -> bytes:
        """自定义加密方案（XOR + 多轮混淆）"""
        if len(key) < 16:
            raise ValueError("自定义加密需要至少16字节密钥")

        # 第一轮：XOR加密
        result = bytearray(data)
        key_len = len(key)

        for i in range(len(result)):
            result[i] ^= key[i % key_len]

        # 第二轮：字节移位
        shift = key[0] % 8
        if shift > 0:
            for i in range(len(result)):
                result[i] = ((result[i] << shift) | (result[i] >> (8 - shift))) & 0xFF

        # 第三轮：密钥流混淆
        stream_key = hashlib.sha256(key + b'stream').digest()
        for i in range(len(result)):
            result[i] ^= stream_key[i % len(stream_key)]

        # 添加HMAC-SHA256认证
        hmac_key = hashlib.sha256(key + b'hmac').digest()
        auth_tag = hmac.new(hmac_key, bytes(result), hashlib.sha256).digest()

        return bytes(result) + auth_tag

    def decrypt_custom(self, encrypted_data: bytes, key: bytes) -> bytes:
        """自定义解密方案"""
        if len(key) < 16:
            raise ValueError("自定义加密需要至少16字节密钥")

        # 分离数据和认证标签
        data = encrypted_data[:-32]
        received_tag = encrypted_data[-32:]

        # 验证HMAC
        hmac_key = hashlib.sha256(key + b'hmac').digest()
        computed_tag = hmac.new(hmac_key, data, hashlib.sha256).digest()

        if not hmac.compare_digest(computed_tag, received_tag):
            raise ValueError("认证失败：数据可能被篡改")

        # 反向第三轮：密钥流混淆
        result = bytearray(data)
        stream_key = hashlib.sha256(key + b'stream').digest()
        for i in range(len(result)):
            result[i] ^= stream_key[i % len(stream_key)]

        # 反向第二轮：字节移位
        shift = key[0] % 8
        if shift > 0:
            for i in range(len(result)):
                result[i] = ((result[i] >> shift) | (result[i] << (8 - shift))) & 0xFF

        # 反向第一轮：XOR加密
        key_len = len(key)
        for i in range(len(result)):
            result[i] ^= key[i % key_len]

        return bytes(result)

    def encrypt_xor(self, data: bytes, key: int) -> bytes:
        """简单XOR加密（不安全，仅用于兼容）"""
        result = bytearray(data)
        for i in range(len(result)):
            result[i] ^= (key >> (8 * (i % 4))) & 0xFF
        return bytes(result)

    def decrypt_xor(self, data: bytes, key: int) -> bytes:
        """简单XOR解密（不安全，仅用于兼容）"""
        return self.encrypt_xor(data, key)  # XOR是对称的

    def encrypt(self, data: bytes, password: str = None, key: bytes = None, **kwargs) -> Dict[str, Any]:
        """加密数据"""
        if self.encryption_type == EncryptionType.NONE:
            return {
                'type': EncryptionType.NONE,
                'data': data,
                'metadata': {}
            }

        elif self.encryption_type == EncryptionType.XOR:
            xor_key = kwargs.get('xor_key', 0xABCD1234)
            return {
                'type': EncryptionType.XOR,
                'data': self.encrypt_xor(data, xor_key),
                'metadata': {'xor_key': xor_key}
            }

        elif self.encryption_type == EncryptionType.AES256_GCM:
            if key is None:
                salt = os.urandom(16)
                key = self.derive_key(password or "default_password", salt, 32)
                metadata = {'salt': salt, 'derived': True}
            else:
                metadata = {'derived': False}

            encrypted = self.encrypt_aes256_gcm(data, key)
            return {
                'type': EncryptionType.AES256_GCM,
                'data': encrypted,
                'metadata': metadata
            }

        elif self.encryption_type == EncryptionType.CHACHA20_POLY1305:
            if key is None:
                salt = os.urandom(16)
                key = self.derive_key(password or "default_password", salt, 32)
                metadata = {'salt': salt, 'derived': True}
            else:
                metadata = {'derived': False}

            encrypted = self.encrypt_chacha20_poly1305(data, key)
            return {
                'type': EncryptionType.CHACHA20_POLY1305,
                'data': encrypted,
                'metadata': metadata
            }

        elif self.encryption_type == EncryptionType.CUSTOM:
            if key is None:
                key = hashlib.sha256((password or "default_password").encode()).digest()

            encrypted = self.encrypt_custom(data, key)
            return {
                'type': EncryptionType.CUSTOM,
                'data': encrypted,
                'metadata': {'derived': key is None}
            }

        else:
            raise ValueError(f"不支持的加密类型: {self.encryption_type}")

    def decrypt(self, encrypted_data: Dict[str, Any], password: str = None, key: bytes = None) -> bytes:
        """解密数据"""
        enc_type = encrypted_data['type']
        data = encrypted_data['data']
        metadata = encrypted_data.get('metadata', {})

        if enc_type == EncryptionType.NONE:
            return data

        elif enc_type == EncryptionType.XOR:
            xor_key = metadata.get('xor_key', 0xABCD1234)
            return self.decrypt_xor(data, xor_key)

        elif enc_type == EncryptionType.AES256_GCM:
            if key is None and metadata.get('derived', False):
                salt = metadata.get('salt')
                key = self.derive_key(password or "default_password", salt, 32)
            elif key is None:
                raise ValueError("需要提供密钥")

            return self.decrypt_aes256_gcm(data, key)

        elif enc_type == EncryptionType.CHACHA20_POLY1305:
            if key is None and metadata.get('derived', False):
                salt = metadata.get('salt')
                key = self.derive_key(password or "default_password", salt, 32)
            elif key is None:
                raise ValueError("需要提供密钥")

            return self.decrypt_chacha20_poly1305(data, key)

        elif enc_type == EncryptionType.CUSTOM:
            if key is None:
                key = hashlib.sha256((password or "default_password").encode()).digest()

            return self.decrypt_custom(data, key)

        else:
            raise ValueError(f"不支持的解密类型: {enc_type}")


def create_encryptor(encryption_type: str = "aes256_gcm") -> SecureEncryptor:
    """创建加密器"""
    type_map = {
        "none": EncryptionType.NONE,
        "xor": EncryptionType.XOR,
        "aes256_gcm": EncryptionType.AES256_GCM,
        "chacha20_poly1305": EncryptionType.CHACHA20_POLY1305,
        "custom": EncryptionType.CUSTOM
    }

    enc_type = type_map.get(encryption_type.lower(), EncryptionType.AES256_GCM)
    return SecureEncryptor(enc_type)


# 测试函数
def test_encryption():
    """测试加密功能"""
    test_data = b"Hello, World! This is a test message for encryption."
    password = "test_password_123"

    print("测试加密功能...")
    print(f"原始数据: {test_data.decode()}")
    print(f"数据长度: {len(test_data)} 字节")

    # 测试各种加密方案
    encryption_types = ["none", "xor", "aes256_gcm", "chacha20_poly1305", "custom"]

    for enc_type in encryption_types:
        print(f"\n--- 测试 {enc_type.upper()} ---")
        try:
            encryptor = create_encryptor(enc_type)

            # 加密
            encrypted = encryptor.encrypt(test_data, password)
            print(f"加密成功: {len(encrypted['data'])} 字节")

            # 解密
            decrypted = encryptor.decrypt(encrypted, password)
            print(f"解密成功: {decrypted.decode()}")

            # 验证
            if test_data == decrypted:
                print("✓ 验证通过")
            else:
                print("✗ 验证失败")

        except Exception as e:
            print(f"✗ 错误: {str(e)}")


if __name__ == "__main__":
    test_encryption()