# Near-duplicate grouping design spike

`-similar` already answers a one-reference question with exact word-shingle Jaccard similarity. A
run-wide grouping operation has a different scaling problem: comparing every pair is quadratic before
the exact verifier has learned anything about either document.

The current exact primitive is byte based: words contain ASCII alphanumerics or high-bit bytes,
and only ASCII case folds. It does not decode Unicode or fold non-ASCII letters. Per-comparison
representation improvements preserve that contract. Reusing a peer representation across entries
requires an immutable source or an explicit mutation/invalidation contract; a dynamic target template
alone does not establish either.

## Fixed correctness boundary

Candidate generation may over-select. It must never emit a relationship. Every candidate pair passes
through `WordShinglePercent` with the requested width and threshold, and only that exact result can add
an edge. Hash collisions therefore cost verification time but cannot create a false match.

For a positive threshold, two non-empty shingle sets must share at least one shingle. An inverted
shingle index can consequently enumerate every possible match without comparing disjoint sets. Empty
sets need one explicit bucket because the current exact contract rates two empty sets as 100% similar.

## Measured spike

`//xff/matching/similarity:near_duplicate_benchmark` compares exhaustive verification with a complete
inverted-shingle candidate index. It validates that both paths emit exactly the same pair set before
timing either path. The matrix includes clustered documents, where postings are selective, and shared
boilerplate, where one common shingle deliberately degenerates the index toward all-pairs work.

On the initial 1,024-document clustered corpus, exhaustive search verified 523,776 pairs while the
index verified 3,584: a 146-fold deterministic reduction. One local optimized run took approximately
15.1 seconds and 0.14 seconds respectively; timings are machine-specific, while the pair counts are
the important architectural evidence. The boilerplate case remains in the benchmark so an optimization
cannot hide the adversarial shape.

## Implementation direction

1. Generate normalized shingles once per eligible entry rather than rebuilding both sets for every
   comparison.
2. Store fixed-width shingle hashes in postings, not duplicate shingle strings. A collision only adds a
   candidate because exact verification remains authoritative.
3. Bound memory by processing partitions when the estimated posting data exceeds the configured
   budget. This can remain in the core while its representation is compact; make it an extension only
   if measured dependencies or retained state become substantial.
4. Keep candidate generation replaceable. Very common postings should later be suppressed or augmented
   by MinHash/LSH, but those mechanisms are optimizations rather than correctness dependencies.

The remaining product decision is clustering, not matching: similarity is not transitive, so connected
components, representative-centered groups, and maximal cliques produce different answers. No CLI
spelling should ship until that behavior is chosen and documented.
