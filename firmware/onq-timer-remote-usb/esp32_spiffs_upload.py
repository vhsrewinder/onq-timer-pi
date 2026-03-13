#!/usr/bin/env python3
"""
ESP32 SPIFFS Data Upload Tool for Arduino IDE 2.x
A modern command-line tool to upload files to ESP32 SPIFFS partition

Usage:
    python esp32_spiffs_upload.py [options]
    
Options:
    --data-dir PATH         Path to data directory (default: ./data)
    --port PORT            Serial port or IP address for upload
    --partition NAME       Partition name (default: spiffs)
    --partition-file PATH  Custom partition CSV file
    --board BOARD          Board name (default: esp32dev)
    --baud RATE           Upload baud rate (default: 921600)
    --flash-mode MODE     Flash mode (default: dio)
    --flash-freq FREQ     Flash frequency (default: 80m)
    --no-upload           Only create image, don't upload
    --verbose             Enable verbose output
    --ota                 Use OTA upload instead of serial
    --ota-port PORT       OTA port (default: 3232)
"""

import os
import sys
import argparse
import subprocess
import platform
import csv
import shutil
import tempfile
from pathlib import Path
import json

# Import our config loader
try:
    from config import ESP32Config
    CONFIG_AVAILABLE = True
except ImportError:
    CONFIG_AVAILABLE = False

class ESP32SPIFFSUploader:
    def __init__(self, config_file=None):
        self.system = platform.system().lower()
        self.verbose = False
        
        # Load configuration if available
        if CONFIG_AVAILABLE and config_file:
            self.config = ESP32Config(config_file)
        elif CONFIG_AVAILABLE:
            self.config = ESP32Config()
        else:
            self.config = None
        
        # Default board configurations (can be overridden by config file)
        self.board_configs = {
            'esp32dev': {
                'mcu': 'esp32',
                'flash_mode': 'dio',
                'flash_freq': '80m',
                'partition_scheme': 'default'
            },
            'esp32s2': {
                'mcu': 'esp32s2',
                'flash_mode': 'dio',
                'flash_freq': '80m',
                'partition_scheme': 'default'
            },
            'esp32s3': {
                'mcu': 'esp32s3',
                'flash_mode': 'dio',
                'flash_freq': '80m',
                'partition_scheme': 'default'
            },
            'esp32c3': {
                'mcu': 'esp32c3',
                'flash_mode': 'dio',
                'flash_freq': '80m',
                'partition_scheme': 'default'
            }
        }
    
    def get_config_value(self, section, key, default=None):
        """Get value from config file or return default"""
        if self.config:
            return self.config.get(section, key, default)
        return default
        
    def log(self, message, force=False):
        """Print message if verbose mode is enabled"""
        if self.verbose or force:
            print(f"[SPIFFS] {message}")
            
    def error(self, message):
        """Print error message and exit"""
        print(f"[ERROR] {message}", file=sys.stderr)
        sys.exit(1)
        
    def find_tool(self, tool_name, search_paths=None):
        """Find a tool in system PATH or specified paths"""
        if search_paths is None:
            search_paths = []
            
        original_tool_name = tool_name
        # Add file extension for Windows
        if self.system == 'windows':
            if not tool_name.endswith('.exe'):
                tool_name += '.exe'
                
        # Check in specified paths first
        for base_path in search_paths:
            # Direct path check
            tool_path = Path(base_path) / tool_name
            if tool_path.exists() and tool_path.is_file():
                return str(tool_path)
            
            # Check in subdirectories (for version folders)
            base_path_obj = Path(base_path)
            if base_path_obj.exists():
                for item in base_path_obj.iterdir():
                    if item.is_dir():
                        # Check in version folder
                        version_tool_path = item / tool_name
                        if version_tool_path.exists():
                            return str(version_tool_path)
                        
                        # Check in bin subfolder
                        bin_tool_path = item / "bin" / tool_name
                        if bin_tool_path.exists():
                            return str(bin_tool_path)
                        
                        # For esptool, also check for esptool.py
                        if original_tool_name == 'esptool':
                            py_tool_path = item / "esptool.py"
                            if py_tool_path.exists():
                                return str(py_tool_path)
                
        # Check in system PATH
        tool_path = shutil.which(tool_name)
        if tool_path:
            return tool_path
            
        return None
        
    def find_arduino_tools(self):
        """Find Arduino tools directory"""
        possible_paths = []
        
        if self.system == 'windows':
            possible_paths = [
                os.path.expanduser("~/AppData/Local/Arduino15/packages/esp32/tools"),
                "C:/Users/*/AppData/Local/Arduino15/packages/esp32/tools",
            ]
        elif self.system == 'darwin':  # macOS
            possible_paths = [
                os.path.expanduser("~/Library/Arduino15/packages/esp32/tools"),
            ]
        else:  # Linux
            possible_paths = [
                os.path.expanduser("~/.arduino15/packages/esp32/tools"),
            ]
            
        for base_path in possible_paths:
            if os.path.exists(base_path):
                return base_path
                
        return None
        
    def parse_partition_table(self, partition_file, partition_name='spiffs'):
        """Parse partition CSV file to find SPIFFS partition info"""
        self.log(f"Parsing partition file: {partition_file}")
        
        if not os.path.exists(partition_file):
            self.error(f"Partition file not found: {partition_file}")
            
        try:
            with open(partition_file, 'r') as f:
                reader = csv.reader(f)
                for row in reader:
                    # Skip comments and empty lines
                    if not row or row[0].strip().startswith('#'):
                        continue
                        
                    if len(row) >= 5:
                        name = row[0].strip()
                        type_field = row[1].strip()
                        subtype = row[2].strip()
                        offset = row[3].strip()
                        size = row[4].strip()
                        
                        if name == partition_name or (partition_name == 'spiffs' and subtype == 'spiffs'):
                            # Parse offset and size (handle hex values)
                            if offset.startswith('0x'):
                                offset_int = int(offset, 16)
                            else:
                                offset_int = int(offset)
                                
                            if size.startswith('0x'):
                                size_int = int(size, 16)
                            else:
                                # Handle size suffixes (K, M)
                                size = size.upper()
                                if size.endswith('K'):
                                    size_int = int(size[:-1]) * 1024
                                elif size.endswith('M'):
                                    size_int = int(size[:-1]) * 1024 * 1024
                                else:
                                    size_int = int(size)
                                    
                            self.log(f"Found partition '{name}': offset=0x{offset_int:x}, size={size_int}")
                            return offset_int, size_int
                            
        except Exception as e:
            self.error(f"Error parsing partition file: {e}")
            
        self.error(f"Partition '{partition_name}' not found in partition table")
        
    def count_files(self, data_dir):
        """Count files in data directory"""
        count = 0
        if os.path.exists(data_dir):
            for root, dirs, files in os.walk(data_dir):
                for file in files:
                    if not file.startswith('.'):
                        count += 1
        return count
        
    def create_spiffs_image(self, data_dir, output_file, size, page_size=256, block_size=4096):
        """Create SPIFFS image using mkspiffs tool"""
        self.log("Creating SPIFFS image...")
        
        # Find mkspiffs tool
        arduino_tools = self.find_arduino_tools()
        search_paths = []
        if arduino_tools:
            search_paths.append(os.path.join(arduino_tools, "mkspiffs"))
            
        mkspiffs_path = self.find_tool('mkspiffs', search_paths)
        if not mkspiffs_path:
            self.error("mkspiffs tool not found. Please install ESP32 Arduino Core.")
            
        # Ensure data directory exists
        if not os.path.exists(data_dir):
            os.makedirs(data_dir)
            self.log(f"Created data directory: {data_dir}")
            
        # Count files
        file_count = self.count_files(data_dir)
        self.log(f"Found {file_count} files in data directory")
        
        if file_count == 0:
            response = input("No files found in data directory. Create empty SPIFFS image? (y/N): ")
            if response.lower() != 'y':
                self.error("SPIFFS creation cancelled")
                
        # Create SPIFFS image
        cmd = [
            mkspiffs_path,
            '-c', data_dir,
            '-p', str(page_size),
            '-b', str(block_size),
            '-s', str(size),
            output_file
        ]
        
        self.log(f"Running: {' '.join(cmd)}")
        self.log(f"Data dir: {data_dir}")
        self.log(f"Size: {size} bytes ({size//1024} KB)")
        self.log(f"Page size: {page_size}")
        self.log(f"Block size: {block_size}")
        
        try:
            result = subprocess.run(cmd, capture_output=True, text=True)
            if result.returncode != 0:
                self.error(f"mkspiffs failed: {result.stderr}")
            else:
                self.log("SPIFFS image created successfully")
                if self.verbose and result.stdout:
                    print(result.stdout)
        except Exception as e:
            self.error(f"Error running mkspiffs: {e}")
            
    def upload_serial(self, image_file, port, offset, board_config, baud_rate):
        """Upload SPIFFS image via serial using esptool"""
        self.log("Uploading SPIFFS image via serial...")
        
        # Find esptool
        arduino_tools = self.find_arduino_tools()
        search_paths = []
        if arduino_tools:
            search_paths.extend([
                os.path.join(arduino_tools, "esptool_py"),
                os.path.join(arduino_tools, "esptool")
            ])
            
        esptool_path = self.find_tool('esptool', search_paths)
        if not esptool_path:
            esptool_path = self.find_tool('esptool.py', search_paths)
            
        if not esptool_path:
            self.error("esptool not found. Please install ESP32 Arduino Core.")
            
        cmd = [
            'python' if esptool_path.endswith('.py') else esptool_path,
        ]
        
        if esptool_path.endswith('.py'):
            cmd.append(esptool_path)
            
        cmd.extend([
            '--chip', board_config['mcu'],
            '--baud', str(baud_rate),
            '--port', port,
            '--before', 'default_reset',
            '--after', 'hard_reset',
            'write_flash',
            '-z',
            '--flash_mode', board_config['flash_mode'],
            '--flash_freq', board_config['flash_freq'],
            '--flash_size', 'detect',
            f'0x{offset:x}',
            image_file
        ])
        
        self.log(f"Upload command: {' '.join(cmd)}")
        self.log(f"Port: {port}")
        self.log(f"Baud rate: {baud_rate}")
        self.log(f"Flash offset: 0x{offset:x}")
        
        try:
            result = subprocess.run(cmd, text=True)
            if result.returncode == 0:
                self.log("SPIFFS image uploaded successfully!")
            else:
                self.error("Upload failed!")
        except Exception as e:
            self.error(f"Error during upload: {e}")
            
    def upload_ota(self, image_file, ip_address, ota_port):
        """Upload SPIFFS image via OTA using espota"""
        self.log("Uploading SPIFFS image via OTA...")
        
        # Find espota
        arduino_tools = self.find_arduino_tools()
        search_paths = []
        if arduino_tools:
            search_paths.append(os.path.join(arduino_tools, "espota"))
            
        espota_path = self.find_tool('espota', search_paths)
        if not espota_path:
            espota_path = self.find_tool('espota.py', search_paths)
            
        if not espota_path:
            self.error("espota tool not found. Please install ESP32 Arduino Core.")
            
        cmd = [
            'python' if espota_path.endswith('.py') else espota_path,
        ]
        
        if espota_path.endswith('.py'):
            cmd.append(espota_path)
            
        cmd.extend([
            '-i', ip_address,
            '-p', str(ota_port),
            '-s',
            '-f', image_file
        ])
        
        self.log(f"OTA command: {' '.join(cmd)}")
        self.log(f"IP address: {ip_address}")
        self.log(f"OTA port: {ota_port}")
        
        try:
            result = subprocess.run(cmd, text=True)
            if result.returncode == 0:
                self.log("SPIFFS image uploaded successfully via OTA!")
            else:
                self.error("OTA upload failed!")
        except Exception as e:
            self.error(f"Error during OTA upload: {e}")
            
    def is_ip_address(self, address):
        """Check if address is an IP address"""
        parts = address.split('.')
        return len(parts) == 4 and all(part.isdigit() and 0 <= int(part) <= 255 for part in parts)
        
    def find_default_partition_file(self, board):
        """Find default partition file for board"""
        arduino_tools = self.find_arduino_tools()
        if not arduino_tools:
            return None
            
        # Look for partition files in ESP32 core
        base_path = Path(arduino_tools).parent / "hardware" / "esp32"
        
        # Find the latest version
        versions = []
        if base_path.exists():
            for item in base_path.iterdir():
                if item.is_dir() and item.name.replace('.', '').isdigit():
                    versions.append(item)
                    
        if not versions:
            return None
            
        latest_version = max(versions, key=lambda x: x.name)
        partitions_dir = latest_version / "tools" / "partitions"
        
        if partitions_dir.exists():
            default_file = partitions_dir / "default.csv"
            if default_file.exists():
                return str(default_file)
                
        return None

def main():
    parser = argparse.ArgumentParser(
        description="ESP32 SPIFFS Data Upload Tool for Arduino IDE 2.x",
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    
    # Load config to get defaults
    config = None
    if CONFIG_AVAILABLE:
        config = ESP32Config()
    
    # Get defaults from config or use hardcoded defaults
    default_port = config.get("upload", "port") if config else "COM3"
    default_baud = config.get("upload", "baud_rate") if config else 921600
    default_data_dir = config.get("upload", "data_directory") if config else "./data"
    default_board = config.get("upload", "board") if config else "esp32dev"
    default_flash_mode = config.get("board_settings", "flash_mode") if config else "dio"
    default_flash_freq = config.get("board_settings", "flash_frequency") if config else "80m"
    default_verbose = config.get("development", "verbose") if config else False
    
    parser.add_argument('--config', default='esp32_spiffs_config.json',
                       help='Configuration file path (default: esp32_spiffs_config.json)')
    parser.add_argument('--data-dir', default=default_data_dir,
                       help=f'Path to data directory (default: {default_data_dir})')
    parser.add_argument('--port', default=default_port,
                       help=f'Serial port or IP address for upload (default: {default_port})')
    parser.add_argument('--partition', default='spiffs',
                       help='Partition name (default: spiffs)')
    parser.add_argument('--partition-file',
                       help='Custom partition CSV file')
    parser.add_argument('--board', default=default_board,
                       help=f'Board name (default: {default_board})')
    parser.add_argument('--baud', type=int, default=default_baud,
                       help=f'Upload baud rate (default: {default_baud})')
    parser.add_argument('--flash-mode', default=default_flash_mode,
                       help=f'Flash mode (default: {default_flash_mode})')
    parser.add_argument('--flash-freq', default=default_flash_freq,
                       help=f'Flash frequency (default: {default_flash_freq})')
    parser.add_argument('--no-upload', action='store_true',
                       help='Only create image, don\'t upload')
    parser.add_argument('--verbose', '-v', action='store_true', default=default_verbose,
                       help=f'Enable verbose output (default: {default_verbose})')
    parser.add_argument('--ota', action='store_true',
                       help='Use OTA upload instead of serial')
    parser.add_argument('--ota-port', type=int, default=3232,
                       help='OTA port (default: 3232)')
    
    args = parser.parse_args()
    
    # Create uploader with config file
    uploader = ESP32SPIFFSUploader(args.config)
    uploader.verbose = args.verbose
    
    # If using config file and no explicit port specified, check for OTA IP
    if config and args.port == default_port and config.get("ota", "enabled"):
        if args.ota:
            args.port = config.get("ota", "ip_address")
    
    # Get board configuration
    if args.board in uploader.board_configs:
        board_config = uploader.board_configs[args.board].copy()
    else:
        uploader.log(f"Unknown board '{args.board}', using esp32dev defaults")
        board_config = uploader.board_configs['esp32dev'].copy()
        
    # Override with command line arguments
    board_config['flash_mode'] = args.flash_mode
    board_config['flash_freq'] = args.flash_freq
    
    # Find partition file
    partition_file = args.partition_file
    if not partition_file:
        partition_file = uploader.find_default_partition_file(args.board)
        if not partition_file:
            uploader.error("No partition file found. Please specify --partition-file")
            
    uploader.log(f"Using partition file: {partition_file}")
    
    # Parse partition table
    offset, size = uploader.parse_partition_table(partition_file, args.partition)
    
    # Create temporary image file
    with tempfile.NamedTemporaryFile(suffix='.spiffs.bin', delete=False) as tmp:
        image_file = tmp.name
        
    try:
        # Create SPIFFS image
        uploader.create_spiffs_image(args.data_dir, image_file, size)
        
        if not args.no_upload:
            # Determine upload method
            if args.ota or uploader.is_ip_address(args.port):
                uploader.upload_ota(image_file, args.port, args.ota_port)
            else:
                uploader.upload_serial(image_file, args.port, offset, board_config, args.baud)
        else:
            uploader.log(f"SPIFFS image created: {image_file}")
            # Don't delete the temp file if we're not uploading
            return
            
    finally:
        # Clean up temporary file
        if os.path.exists(image_file):
            os.unlink(image_file)

if __name__ == '__main__':
    main()