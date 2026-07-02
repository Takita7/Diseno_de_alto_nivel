#!/usr/bin/env python3
#
# config_parser.py - Python configuration parsing utility
#
# Provides configuration loading, parameter access, and validation
#

import json
import yaml
from pathlib import Path
from typing import Any, Dict, Optional

class ConfigParser:
    """Configuration parser for YAML and JSON files."""
    
    def __init__(self):
        self.params: Dict[str, Any] = {}
    
    @classmethod
    def load_yaml(cls, config_file: str) -> 'ConfigParser':
        """Load configuration from YAML file."""
        parser = cls()
        config_path = Path(config_file)
        
        if not config_path.exists():
            raise FileNotFoundError(f"Configuration file not found: {config_file}")
        
        try:
            with open(config_path, 'r') as f:
                parser.params = yaml.safe_load(f) or {}
        except yaml.YAMLError as e:
            raise ValueError(f"Invalid YAML in config file: {e}")
        
        return parser
    
    @classmethod
    def load_json(cls, config_file: str) -> 'ConfigParser':
        """Load configuration from JSON file."""
        parser = cls()
        config_path = Path(config_file)
        
        if not config_path.exists():
            raise FileNotFoundError(f"Configuration file not found: {config_file}")
        
        try:
            with open(config_path, 'r') as f:
                parser.params = json.load(f)
        except json.JSONDecodeError as e:
            raise ValueError(f"Invalid JSON in config file: {e}")
        
        return parser
    
    def get(self, key: str, default: Any = None) -> Any:
        """Get configuration parameter by dot-separated key."""
        keys = key.split('.')
        value = self.params
        
        for k in keys:
            if isinstance(value, dict) and k in value:
                value = value[k]
            else:
                return default
        
        return value
    
    def set(self, key: str, value: Any) -> None:
        """Set configuration parameter by dot-separated key."""
        keys = key.split('.')
        current = self.params
        
        for k in keys[:-1]:
            if k not in current:
                current[k] = {}
            current = current[k]
        
        current[keys[-1]] = value
    
    def get_all(self) -> Dict[str, Any]:
        """Get all configuration parameters."""
        return self.params.copy()
    
    def save(self, output_file: str, format: str = 'yaml') -> None:
        """Save configuration to file."""
        output_path = Path(output_file)
        output_path.parent.mkdir(parents=True, exist_ok=True)
        
        with open(output_path, 'w') as f:
            if format.lower() == 'json':
                json.dump(self.params, f, indent=2)
            else:  # default to YAML
                yaml.dump(self.params, f, default_flow_style=False)
    
    def print_summary(self) -> None:
        """Print configuration summary."""
        print("Configuration Summary:")
        print("=" * 50)
        self._print_dict(self.params)
        print("=" * 50)
    
    def _print_dict(self, d: Dict[str, Any], indent: int = 0) -> None:
        """Recursively print configuration dictionary."""
        for key, value in d.items():
            if isinstance(value, dict):
                print(" " * indent + f"{key}:")
                self._print_dict(value, indent + 2)
            else:
                print(" " * indent + f"{key}: {value}")

if __name__ == "__main__":
    import argparse
    
    parser = argparse.ArgumentParser(description="Configuration parser utility")
    parser.add_argument("config_file", help="Configuration file to parse")
    parser.add_argument("--key", help="Get specific parameter")
    parser.add_argument("--format", choices=["yaml", "json"], default="yaml",
                        help="Configuration file format")
    parser.add_argument("--print", action="store_true", help="Print configuration")
    
    args = parser.parse_args()
    
    if args.format == "json":
        config = ConfigParser.load_json(args.config_file)
    else:
        config = ConfigParser.load_yaml(args.config_file)
    
    if args.key:
        value = config.get(args.key)
        print(f"{args.key}: {value}")
    elif args.print:
        config.print_summary()
    else:
        config.print_summary()
