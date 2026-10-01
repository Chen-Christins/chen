#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
XLSX转CSV工具
支持将XLSX文件转换为CSV格式，方便数据处理和交换
"""

import os
import sys
import csv
import argparse
from typing import Dict, List, Any, Optional
import openpyxl
from openpyxl import load_workbook
from openpyxl.utils.cell import coordinate_from_string
import chardet

class XlsxToCsvConverter:
    """XLSX到CSV的转换器"""

    def __init__(self, default_encoding: str = 'utf-8', default_delimiter: str = ','):
        self.default_encoding = default_encoding
        self.default_delimiter = default_delimiter

    def _read_xlsx_data(self, file_path: str, sheet_name: Optional[str] = None) -> Dict[str, Any]:
        """读取XLSX文件数据"""
        try:
            wb = load_workbook(file_path, read_only=True, data_only=True)

            # 选择工作表
            if sheet_name:
                if sheet_name not in wb.sheetnames:
                    raise ValueError(f"工作表 '{sheet_name}' 不存在，可用的工作表: {wb.sheetnames}")
                ws = wb[sheet_name]
            else:
                # 使用第一个工作表
                ws = wb.active
                sheet_name = ws.title
                print(f"使用工作表: {sheet_name}")

            # 读取所有数据
            data = []
            for row in ws.iter_rows(values_only=True):
                # 跳过完全为空的行
                if not any(cell is not None and str(cell).strip() for cell in row):
                    continue
                # 转换None为空字符串
                processed_row = [str(cell) if cell is not None else '' for cell in row]
                data.append(processed_row)

            if not data:
                raise ValueError(f"工作表 '{sheet_name}' 为空")

            # 第一行作为表头（如果没有数据则使用默认列名）
            headers = data[0]
            data_rows = data[1:]

            # 清理表头
            headers = [h.strip() if h.strip() else f"Column_{i+1}" for i, h in enumerate(headers)]

            # 清理数据行
            cleaned_data = []
            for row in data_rows:
                # 确保行的长度与表头一致
                while len(row) < len(headers):
                    row.append('')
                # 截断过长的行
                if len(row) > len(headers):
                    row = row[:len(headers)]
                cleaned_data.append(row)

            wb.close()

            return {
                'headers': headers,
                'data': cleaned_data,
                'row_count': len(cleaned_data),
                'sheet_name': sheet_name,
                'all_sheets': wb.sheetnames if 'wb' in locals() else []
            }

        except Exception as e:
            raise Exception(f"读取XLSX文件失败 {file_path}: {str(e)}")

    def _create_csv(self, xlsx_data: Dict[str, Any], output_path: str,
                    encoding: str, delimiter: str, quoting: int = csv.QUOTE_MINIMAL) -> None:
        """创建CSV文件"""
        headers = xlsx_data['headers']
        data = xlsx_data['data']

        try:
            with open(output_path, 'w', encoding=encoding, newline='') as f:
                writer = csv.writer(f, delimiter=delimiter, quoting=quoting)

                # 写入表头
                writer.writerow(headers)

                # 写入数据行
                for row in data:
                    writer.writerow(row)

        except Exception as e:
            raise Exception(f"创建CSV文件失败 {output_path}: {str(e)}")

    def convert_xlsx_to_csv(self, xlsx_files: List[str], output_pattern: str,
                           encoding: str = None, delimiter: str = None,
                           sheet_name: str = None, quoting: int = csv.QUOTE_MINIMAL) -> None:
        """转换XLSX文件为CSV格式"""
        encoding = encoding or self.default_encoding
        delimiter = delimiter or self.default_delimiter

        print(f"开始转换 {len(xlsx_files)} 个XLSX文件...")
        print(f"使用编码: {encoding}")
        print(f"使用分隔符: '{delimiter}'")

        # 检查输出是否为目录
        is_output_dir = output_pattern.endswith('/') or output_pattern.endswith('\\') or os.path.isdir(output_pattern)

        # 创建输出目录
        if is_output_dir:
            output_dir = output_pattern if os.path.isdir(output_pattern) else output_pattern
            if not os.path.exists(output_dir):
                os.makedirs(output_dir)
        else:
            output_dir = os.path.dirname(output_pattern)
            if output_dir and not os.path.exists(output_dir):
                os.makedirs(output_dir)

        converted_files = []

        for xlsx_file in xlsx_files:
            if not os.path.exists(xlsx_file):
                print(f"警告: 文件不存在 {xlsx_file}")
                continue

            print(f"\n处理文件: {xlsx_file}")

            try:
                # 读取XLSX数据
                xlsx_data = self._read_xlsx_data(xlsx_file, sheet_name)
                print(f"  - 工作表: {xlsx_data['sheet_name']}")
                print(f"  - 字段数: {len(xlsx_data['headers'])}")
                print(f"  - 数据行数: {xlsx_data['row_count']}")

                # 生成输出文件名
                if '*' in output_pattern:
                    # 替换通配符
                    base_name = os.path.splitext(os.path.basename(xlsx_file))[0]
                    if sheet_name and len(xlsx_data['all_sheets']) > 1:
                        output_file = output_pattern.replace('*', f"{base_name}_{sheet_name}")
                    else:
                        output_file = output_pattern.replace('*', base_name)
                elif is_output_dir:
                    # 使用输出目录 + 文件名
                    base_name = os.path.splitext(os.path.basename(xlsx_file))[0]
                    if sheet_name and len(xlsx_data['all_sheets']) > 1:
                        output_file = os.path.join(output_dir, f"{base_name}_{sheet_name}.csv")
                    else:
                        output_file = os.path.join(output_dir, f"{base_name}.csv")
                else:
                    # 直接使用指定的输出文件（仅当输入只有一个文件时）
                    if len(xlsx_files) == 1:
                        output_file = output_pattern
                    else:
                        base_name = os.path.splitext(os.path.basename(xlsx_file))[0]
                        if sheet_name and len(xlsx_data['all_sheets']) > 1:
                            output_file = os.path.join(output_dir, f"{base_name}_{sheet_name}.csv")
                        else:
                            output_file = os.path.join(output_dir, f"{base_name}.csv")

                # 转换为CSV
                self._create_csv(xlsx_data, output_file, encoding, delimiter, quoting)
                print(f"  -> 输出文件: {output_file}")

                converted_files.append(output_file)

            except Exception as e:
                print(f"  错误: {str(e)}")
                continue

        print(f"\n转换完成! 共转换 {len(converted_files)} 个文件")

        if converted_files:
            print("输出文件:")
            for file in converted_files:
                print(f"  - {file}")

def main():
    parser = argparse.ArgumentParser(description='XLSX到CSV转换工具')
    parser.add_argument('input', nargs='+', help='输入的XLSX文件路径')
    parser.add_argument('-o', '--output', required=True,
                       help='输出的CSV文件路径（可使用*通配符或指定目录）')
    parser.add_argument('-e', '--encoding', default='utf-8',
                       help='指定输出文件编码（默认: utf-8）')
    parser.add_argument('-d', '--delimiter', default=',',
                       help='指定CSV分隔符（默认: ,）')
    parser.add_argument('-s', '--sheet', help='指定工作表名称（默认使用第一个工作表）')
    parser.add_argument('-v', '--verbose', action='store_true', help='显示详细信息')
    parser.add_argument('--quote-all', action='store_true',
                       help='对所有字段加引号')
    parser.add_argument('--quote-nonnumeric', action='store_true',
                       help='对非数值字段加引号')
    parser.add_argument('--quote-none', action='store_true',
                       help='不对任何字段加引号')

    args = parser.parse_args()

    # 设置引号策略
    if args.quote_all:
        quoting = csv.QUOTE_ALL
    elif args.quote_nonnumeric:
        quoting = csv.QUOTE_NONNUMERIC
    elif args.quote_none:
        quoting = csv.QUOTE_NONE
    else:
        quoting = csv.QUOTE_MINIMAL

    converter = XlsxToCsvConverter(
        default_encoding=args.encoding,
        default_delimiter=args.delimiter
    )

    try:
        converter.convert_xlsx_to_csv(
            args.input,
            args.output,
            encoding=args.encoding,
            delimiter=args.delimiter,
            sheet_name=args.sheet,
            quoting=quoting
        )
    except Exception as e:
        print(f"错误: {str(e)}")
        sys.exit(1)

if __name__ == '__main__':
    main()