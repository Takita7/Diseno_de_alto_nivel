#!/usr/bin/env python3
#
# analyze_results.py - Benchmark result analysis utility
#
# Analyzes benchmark results and generates reports
#

import json
import csv
import argparse
from pathlib import Path
from typing import Dict, List, Any
from dataclasses import dataclass
import statistics

@dataclass
class BenchmarkResult:
    name: str
    kernel_name: str
    configuration: str
    execution_time: float  # milliseconds
    throughput: float  # kernels/sec
    resource_util: Dict[str, float]
    metrics: Dict[str, Any]

class ResultAnalyzer:
    """Analyze benchmark results and generate reports."""
    
    def __init__(self):
        self.results: List[BenchmarkResult] = []
    
    def load_results(self, result_file: str) -> None:
        """Load results from JSON file."""
        result_path = Path(result_file)
        
        if not result_path.exists():
            raise FileNotFoundError(f"Result file not found: {result_file}")
        
        with open(result_path, 'r') as f:
            data = json.load(f)
        
        # Parse results
        for result_data in data.get('results', []):
            result = BenchmarkResult(
                name=result_data['name'],
                kernel_name=result_data['kernel_name'],
                configuration=result_data['configuration'],
                execution_time=result_data['execution_time'],
                throughput=result_data['throughput'],
                resource_util=result_data.get('resource_util', {}),
                metrics=result_data.get('metrics', {})
            )
            self.results.append(result)
    
    def print_summary(self) -> None:
        """Print summary of results."""
        if not self.results:
            print("No results to analyze")
            return
        
        print("Benchmark Results Summary")
        print("=" * 70)
        print(f"{'Benchmark':<20} {'Config':<15} {'Time (ms)':<12} {'Throughput':<12}")
        print("-" * 70)
        
        for result in self.results:
            print(f"{result.name:<20} {result.configuration:<15} "
                  f"{result.execution_time:>10.2f} {result.throughput:>10.2f}")
        
        print("=" * 70)
    
    def generate_comparison_report(self, baseline_config: str, 
                                  output_file: str) -> None:
        """Generate comparison report against baseline."""
        baseline_results = {r.name: r for r in self.results 
                           if r.configuration == baseline_config}
        
        report = []
        report.append("Comparison Report Against Baseline")
        report.append("=" * 70)
        report.append(f"Baseline Configuration: {baseline_config}")
        report.append("")
        
        for result in self.results:
            if result.configuration == baseline_config:
                continue
            
            baseline = baseline_results.get(result.name)
            if not baseline:
                continue
            
            speedup = baseline.execution_time / result.execution_time
            efficiency = (speedup - 1.0) / ((result.execution_time / baseline.execution_time) - 1.0) \
                         if baseline.execution_time != result.execution_time else 1.0
            
            report.append(f"{result.name} ({result.configuration}):")
            report.append(f"  Speedup: {speedup:.2f}x")
            report.append(f"  Time Change: {((result.execution_time / baseline.execution_time - 1.0) * 100):.1f}%")
            report.append("")
        
        # Write report
        with open(output_file, 'w') as f:
            f.write("\n".join(report))
        
        print(f"Comparison report saved to {output_file}")

def main():
    parser = argparse.ArgumentParser(description="Analyze benchmark results")
    parser.add_argument("result_file", help="Result file to analyze (JSON)")
    parser.add_argument("--summary", action="store_true", help="Print summary")
    parser.add_argument("--compare", type=str, help="Compare against baseline configuration")
    parser.add_argument("--output", type=str, default="analysis_report.txt",
                        help="Output file for analysis report")
    
    args = parser.parse_args()
    
    analyzer = ResultAnalyzer()
    analyzer.load_results(args.result_file)
    
    if args.summary:
        analyzer.print_summary()
    
    if args.compare:
        analyzer.generate_comparison_report(args.compare, args.output)
    else:
        analyzer.print_summary()

if __name__ == "__main__":
    main()
