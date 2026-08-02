#!/usr/bin/env python3
"""
Fetch mempool transaction data from a Bitcoin Core node.

This script:
1. Calls bitcoin-cli getrawmempool to get all transaction IDs in the mempool
2. Saves the txids to benchmark/getrawmempool.json
3. For each txid, calls bitcoin-cli getrawtransaction to get full transaction data
4. Saves the full transaction data to benchmark/getrawmempool_full.json

Usage:
    python3 fetch_mempool.py [--limit N] [--output-dir DIR]

Options:
    --limit N     Only fetch first N transactions (default: all)
    --output-dir  Output directory (default: benchmark/)
"""

import argparse
import json
import subprocess
import sys
from pathlib import Path


def run_bitcoin_cli(command):
    """Run bitcoin-cli command and return JSON output."""
    try:
        result = subprocess.run(
            ["bitcoin-cli"] + command,
            capture_output=True,
            text=True,
            check=True
        )
        return json.loads(result.stdout)
    except subprocess.CalledProcessError as e:
        print(f"Error running bitcoin-cli {' '.join(command)}: {e.stderr}", file=sys.stderr)
        sys.exit(1)
    except json.JSONDecodeError as e:
        print(f"Error parsing JSON output: {e}", file=sys.stderr)
        print(f"Raw output: {result.stdout}", file=sys.stderr)
        sys.exit(1)


def fetch_mempool_txids():
    """Fetch all transaction IDs from the mempool."""
    print("Fetching mempool transaction IDs...")
    txids = run_bitcoin_cli(["getrawmempool"])
    print(f"Found {len(txids)} transactions in mempool")
    return txids


def fetch_transaction(txid, verbose=True):
    """Fetch a single transaction with verbose output."""
    if verbose:
        return run_bitcoin_cli(["getrawtransaction", txid, "1"])
    else:
        return run_bitcoin_cli(["getrawtransaction", txid])


def main():
    parser = argparse.ArgumentParser(
        description="Fetch mempool transaction data from Bitcoin Core"
    )
    parser.add_argument(
        "--limit",
        type=int,
        default=None,
        help="Limit number of transactions to fetch (default: all)"
    )
    parser.add_argument(
        "--output-dir",
        type=str,
        default="benchmark",
        help="Output directory (default: benchmark/)"
    )
    args = parser.parse_args()

    # Create output directory if it doesn't exist
    output_dir = Path(args.output_dir)
    output_dir.mkdir(parents=True, exist_ok=True)

    # Fetch mempool txids
    txids = fetch_mempool_txids()

    # Save txids to getrawmempool.json
    txids_path = output_dir / "getrawmempool.json"
    with open(txids_path, "w") as f:
        json.dump(txids, f, indent=2)
    print(f"Saved {len(txids)} txids to {txids_path}")

    # Fetch full transactions
    limit = args.limit if args.limit is not None else len(txids)
    print(f"Fetching full transaction data for first {limit} transactions...")

    transactions = []
    for i, txid in enumerate(txids[:limit]):
        try:
            tx = fetch_transaction(txid, verbose=True)
            transactions.append(tx)
            if (i + 1) % 10 == 0:
                print(f"  Processed {i + 1}/{limit}...")
        except Exception as e:
            print(f"  Error fetching txid {txid}: {e}", file=sys.stderr)

    # Save full transactions
    full_path = output_dir / "getrawmempool_full.json"
    with open(full_path, "w") as f:
        json.dump(transactions, f, indent=2)
    print(f"Saved {len(transactions)} full transactions to {full_path}")

    print("\nDone!")


if __name__ == "__main__":
    main()
