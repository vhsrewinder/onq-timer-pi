#!/usr/bin/env python3
"""
Configuration loader for ESP32 SPIFFS Upload Tool
"""

import json
import os
from pathlib import Path

class ESP32Config:
    def __init__(self, config_file="esp32_spiffs_config.json"):
        self.config_file = config_file
        self.config = self.load_config()
    
    def load_config(self):
        """Load configuration from JSON file"""
        # Look for config file in current directory, then parent directories
        config_path = self.find_config_file()
        
        if not config_path:
            print(f"Warning: Config file '{self.config_file}' not found, using defaults")
            return self.get_default_config()
        
        try:
            with open(config_path, 'r') as f:
                config = json.load(f)
                print(f"Loaded config from: {config_path}")
                return self.merge_with_defaults(config)
        except Exception as e:
            print(f"Error loading config file: {e}")
            print("Using default configuration")
            return self.get_default_config()
    
    def find_config_file(self):
        """Find config file in current or parent directories"""
        current_dir = Path.cwd()
        
        # Check current directory and up to 3 parent directories
        for i in range(4):
            config_path = current_dir / self.config_file
            if config_path.exists():
                return config_path
            current_dir = current_dir.parent
            
        return None
    
    def get_default_config(self):
        """Return default configuration"""
        return {
            "project": {
                "name": "ESP32_SPIFFS_Project",
                "description": "ESP32 project with SPIFFS file system",
                "version": "1.0.0"
            },
            "upload": {
                "port": "COM3",
                "baud_rate": 921600,
                "data_directory": "./data",
                "partition_name": "spiffs",
                "board": "esp32dev"
            },
            "board_settings": {
                "flash_mode": "dio",
                "flash_frequency": "80m",
                "flash_size": "4MB"
            },
            "ota": {
                "enabled": True,
                "ip_address": "192.168.1.100",
                "port": 3232,
                "password": ""
            },
            "development": {
                "verbose": True,
                "auto_detect_tools": True,
                "backup_before_upload": False
            },
            "advanced": {
                "partition_file": "",
                "custom_mkspiffs_args": [],
                "custom_esptool_args": [],
                "timeout_seconds": 60
            }
        }
    
    def merge_with_defaults(self, user_config):
        """Merge user config with defaults"""
        default_config = self.get_default_config()
        
        def deep_merge(default, user):
            result = default.copy()
            for key, value in user.items():
                if key in result and isinstance(result[key], dict) and isinstance(value, dict):
                    result[key] = deep_merge(result[key], value)
                else:
                    result[key] = value
            return result
        
        return deep_merge(default_config, user_config)
    
    def get(self, section, key, default=None):
        """Get configuration value"""
        try:
            return self.config[section][key]
        except KeyError:
            return default
    
    def get_upload_args(self):
        """Get command line arguments for upload tool"""
        args = []
        
        # Basic arguments
        args.extend(["--port", self.get("upload", "port")])
        args.extend(["--baud", str(self.get("upload", "baud_rate"))])
        args.extend(["--data-dir", self.get("upload", "data_directory")])
        args.extend(["--partition", self.get("upload", "partition_name")])
        args.extend(["--board", self.get("upload", "board")])
        
        # Board settings
        args.extend(["--flash-mode", self.get("board_settings", "flash_mode")])
        args.extend(["--flash-freq", self.get("board_settings", "flash_frequency")])
        
        # Development settings
        if self.get("development", "verbose"):
            args.append("--verbose")
        
        # Advanced settings
        partition_file = self.get("advanced", "partition_file")
        if partition_file:
            args.extend(["--partition-file", partition_file])
        
        return args
    
    def get_ota_args(self):
        """Get OTA-specific arguments"""
        args = self.get_upload_args()
        
        # Replace port with IP address
        port_index = args.index("--port") + 1
        args[port_index] = self.get("ota", "ip_address")
        
        # Add OTA flag
        args.append("--ota")
        
        # Add OTA port if different from default
        ota_port = self.get("ota", "port")
        if ota_port != 3232:
            args.extend(["--ota-port", str(ota_port)])
        
        return args
    
    def save_config(self, config_file=None):
        """Save current configuration to file"""
        if config_file is None:
            config_file = self.config_file
        
        try:
            with open(config_file, 'w') as f:
                json.dump(self.config, f, indent=2)
            print(f"Configuration saved to: {config_file}")
        except Exception as e:
            print(f"Error saving config: {e}")

# Example usage
if __name__ == "__main__":
    config = ESP32Config()
    
    print("Upload arguments:", config.get_upload_args())
    print("OTA arguments:", config.get_ota_args())
    print("Port:", config.get("upload", "port"))
    print("Verbose:", config.get("development", "verbose"))