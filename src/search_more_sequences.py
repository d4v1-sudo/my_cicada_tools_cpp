import csv
import sys
import os
import re

def load_cross_page_patterns(txt_path):
    sequences = {}
    if not os.path.exists(txt_path): return sequences
    with open(txt_path, 'r', encoding='utf-8') as f:
        for idx, line in enumerate(f):
            # Pattern [ 0 3 20 20 ] found across...
            match = re.search(r'Pattern \[\s*(.*?)\s*\]', line)
            if match:
                seq = match.group(1).split()
                if len(seq) >= 3:
                    sequences[f"Ngram_Pattern_{idx}"] = seq
    return sequences

def load_delta_streams(csv_path):
    sequences = {}
    if not os.path.exists(csv_path): return sequences
    with open(csv_path, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for row in reader:
            page = row['Page']
            delta = row['Delta']
            if page not in sequences:
                sequences[page] = []
            sequences[page].append(delta)
    return sequences

def load_doublets(csv_path):
    sequences = {}
    if not os.path.exists(csv_path): return sequences
    with open(csv_path, 'r', encoding='utf-8') as f:
        reader = csv.DictReader(f)
        for idx, row in enumerate(reader):
            # Format: Page,Position,Word,DoubletType,Values...
            if 'DoubletType' in row and row['DoubletType'] != '':
                # Can't easily extract exact sequence from here, maybe skip or use 'Word' length
                pass
    return sequences

def search_oeis(sequences, oeis_path, output_path):
    target_strings = {}
    for key, seq in sequences.items():
        if len(seq) < 3:
            continue
        # Only take the first 15 numbers to have a realistic chance of matching if it's long, or the whole thing
        # Actually, let's search the whole thing
        target_strings[key] = "," + ",".join(seq) + ","
    
    print(f"Loaded {len(target_strings)} sequences for searching OEIS.")
    
    hits = []
    with open(oeis_path, 'r', encoding='utf-8', errors='ignore') as f:
        for line in f:
            if not line.startswith("A"):
                continue
            parts = line.strip().split(" ", 1)
            if len(parts) != 2:
                continue
            oeis_id = parts[0]
            data_str = parts[1]
            if not data_str.startswith(","): data_str = "," + data_str
            if not data_str.endswith(","): data_str = data_str + ","
            
            for key, target_str in target_strings.items():
                if target_str in data_str:
                    matched_sequence = target_str.strip(',')
                    hits.append((key, oeis_id, matched_sequence))
                    print(f"Match found! {key} -> {oeis_id} (Sequence: {matched_sequence})")

    with open(output_path, 'w', encoding='utf-8') as f:
        for key, oeis_id, seq in hits:
            f.write(f"{key} found in {oeis_id} | Sequence: {seq}\n")
    print(f"Total hits: {len(hits)}")

if __name__ == "__main__":
    cross_path = "../output/cross_page_patterns.txt"
    delta_path = "../output/delta_stream_analysis.csv"
    oeis_path = "../wordlists/oeis.txt"
    output_path = "../output/oeis_more_sequences_hits.txt"
    
    seqs = {}
    seqs.update(load_cross_page_patterns(cross_path))
    # We will only search the first 5 elements of delta streams to see if the start of a page matches something
    # since full delta streams are hundreds of elements and won't match exactly.
    delta_streams = load_delta_streams(delta_path)
    for p, ds in delta_streams.items():
        if len(ds) >= 5:
            seqs[f"Delta_Start_{p}"] = ds[:5]
            # also test middle or some specific length if needed
        
    search_oeis(seqs, oeis_path, output_path)
