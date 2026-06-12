"""
dashboard.py — Cicada 3301 / Liber Primus Analytical Dashboard (Streamlit)
Cobre todos os CSVs e TXTs gerados pelo C++.
"""

import streamlit as st
import pandas as pd
import plotly.express as px
import plotly.graph_objects as go
import os
import numpy as np
import hashlib
import json

st.set_page_config(
    page_title="Cicada 3301 Analyzer",
    layout="wide",
    page_icon="🧩",
)

st.title("🧩 Cicada 3301 — Liber Primus Analytical Dashboard")
st.markdown("Exploring the mathematical and structural mysteries of the Liber Primus.")

OUTPUT_DIR = "../output"
CACHE_DIR  = os.path.join(OUTPUT_DIR, ".cache")
CACHE_FILE = os.path.join(CACHE_DIR, "file_hashes.json")

if not os.path.exists(OUTPUT_DIR):
    st.error(f"Folder {OUTPUT_DIR} not found. Run the C++ program first.")
    st.stop()

# ---------------------------------------------------------------------------
# Cache de hashes (leitura apenas — o dashboard não processa, só lê)
# ---------------------------------------------------------------------------

@st.cache_data(ttl=30)
def load_cache() -> dict:
    if os.path.exists(CACHE_FILE):
        try:
            with open(CACHE_FILE, "r", encoding="utf-8") as f:
                return json.load(f)
        except Exception:
            return {}
    return {}

def file_changed_since_cache(path: str) -> bool:
    """Return True if the file changed since last processing by visualize_correlations.py."""
    if not os.path.exists(path):
        return False
    cache = load_cache()
    size  = os.path.getsize(path)
    sha   = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            sha.update(chunk)
    fp   = {"size": size, "sha256": sha.hexdigest()}
    prev = cache.get(os.path.abspath(path))
    return not (prev and prev.get("size") == fp["size"] and prev.get("sha256") == fp["sha256"])

def stale_warning(path: str) -> None:
    """Show a caption warning if the file was modified and not reprocessed by the analyzer."""
    if file_changed_since_cache(path):
        st.caption(
            "⚠️ This file was modified since the last run of `visualize_correlations.py`. "
            "Run the script to refresh the generated graphs."
        )

@st.cache_data(ttl=60)
def load_csv(path: str) -> pd.DataFrame:
    return pd.read_csv(path)

@st.cache_data(ttl=60)
def load_csv_index(path: str) -> pd.DataFrame:
    return pd.read_csv(path, index_col=0)

# ---------------------------------------------------------------------------
# Sidebar — status dos arquivos
# ---------------------------------------------------------------------------

with st.sidebar:
    st.header("📁 File Status")

    all_files = {
        "corr_runic.csv":           "Runic Correlations",
        "corr_latin.csv":           "Latin Correlations",
        "corr_markov.csv":          "Markov Correlations",
        "page_features.csv":        "Page Metrics",
        "heuristic_scores.csv":     "Cipher Heuristics",
        "interrupt_deltas.csv":     "Interrupt Geometry",
        "key_stream_analysis.csv":  "Key Stream",
        "delta_stream_analysis.csv":"Delta Stream (C[i]-C[i-1])",
        "delta_autocorrelation.csv":"Delta Autocorrelation",
        "cluster_analysis.txt":     "Global Cluster Analysis",
        "rolling_ioc_analysis.txt": "Rolling IoC (log)",
        "kasiski_results.txt":      "Kasiski (log)",
        "friedman_scan.csv":        "Friedman Scan",
        "cluster_mutual_ioc.csv":   "Mutual IoC Cluster",
        "liber_unigram_target.csv": "Target Unigram",
        "corr_runic_report.txt":    "Suspicion Report",
        "MASTER_RESEARCH_SUMMARY.log": "Master Summary",
    }

    for fname, label in all_files.items():
        fpath = os.path.join(OUTPUT_DIR, fname)
        if os.path.exists(fpath):
            size_kb = os.path.getsize(fpath) / 1024
            changed = file_changed_since_cache(fpath)
            icon    = "🔄" if changed else "✅"
            st.markdown(f"{icon} **{label}** `{size_kb:.1f} KB`")
        else:
            st.markdown(f"⬜ {label} *(not generated)*")

    st.divider()
    st.info("🔄 = file changed, re-run `visualize_correlations.py`\n\n✅ = processed and cached")

    if st.button("🔁 Force Refresh"):
        st.cache_data.clear()
        st.rerun()

# ---------------------------------------------------------------------------
# Abas principais
# ---------------------------------------------------------------------------

tabs = st.tabs([
    "📊 Correlations",
    "📉 Metrics",
    "📐 Interrupt Geometry",
    "🔑 Key Stream",
    "🌊 Delta Stream",
    "🧩 Global Clusters",
    "🕵️ Suspicion",
    "🔢 Heuristics",
    "📈 Friedman & Kasiski",
    "🧮 Unigram & Zipf",
    "📋 Master Summary",
])

tab_corr, tab_metr, tab_geom, tab_ks, tab_delta, tab_gcluster, tab_susp, tab_heur, tab_fk, tab_uni, tab_master = tabs

# ── Tab 1: Correlações ──────────────────────────────────────────────────────
with tab_corr:
    st.header("Correlation Matrices")

    matrix_options = [
        f for f in ["corr_runic.csv", "corr_latin.csv", "corr_markov.csv"]
        if os.path.exists(os.path.join(OUTPUT_DIR, f))
    ]
    if not matrix_options:
        st.warning("No correlation matrices generated yet. Run Option 1 in the C++ tool.")
    else:
        col_opt  = st.selectbox("Select matrix", matrix_options)
        csv_path = os.path.join(OUTPUT_DIR, col_opt)
        stale_warning(csv_path)

        df_corr = load_csv_index(csv_path)

        fig_heat = px.imshow(
            df_corr, text_auto=False, aspect="auto",
            color_continuous_scale="RdBu_r",
            title=f"Correlation Heatmap: {col_opt}",
        )
        st.plotly_chart(fig_heat, width="stretch")

        # Top correlations (excluding diagonal)
        mask = np.ones(df_corr.shape, dtype=bool)
        np.fill_diagonal(mask, 0)
        pairs = df_corr.where(mask).stack().reset_index()
        pairs.columns = ["Page A", "Page B", "Correlation"]
        top   = pairs.sort_values("Correlation", ascending=False).head(20)

        col1, col2 = st.columns(2)
        with col1:
            st.subheader("Top 20 Most Correlated Pairs")
            st.dataframe(top.style.background_gradient(subset=["Correlation"], cmap="Reds"), height=400)
        with col2:
            st.subheader("Mean Correlation per Page (Hub Score)")
            hub = df_corr.where(mask).mean().sort_values(ascending=False).reset_index()
            hub.columns = ["Page", "Mean Correlation"]
            fig_hub = px.bar(hub, x="Page", y="Mean Correlation", title="Hub Score by Page")
            st.plotly_chart(fig_hub, width="stretch")

        # Images generated by visualize_correlations.py
        st.subheader("Generated visualizations")
        imgs = {
            "Clustermap":         csv_path.replace(".csv", "_cluster.png"),
            "PCA":                csv_path.replace(".csv", "_pca.png"),
            "Network Graph":      csv_path.replace(".csv", "_network.png"),
            "Cosine Distance":    csv_path.replace(".csv", "_dist_cosine.png"),
        }
        cols = st.columns(2)
        for idx, (label, img_path) in enumerate(imgs.items()):
            with cols[idx % 2]:
                if os.path.exists(img_path):
                    st.image(img_path, caption=label, width="stretch")
                else:
                    st.caption(f"_{label}_ not generated — run `visualize_correlations.py`")

    st.info("High-correlation blocks indicate shared cipher algorithms.")

# ── Tab 2: Métricas ──────────────────────────────────────────────────────────
with tab_metr:
    st.header("Distribution of Metrics per Page")

    feat_path = os.path.join(OUTPUT_DIR, "page_features.csv")
    if not os.path.exists(feat_path):
        st.warning("page_features.csv not found. Run Options 1 or 9 in the C++ tool.")
    else:
        stale_warning(feat_path)
        df_feat = load_csv(feat_path)

        # Filtros interativos
        with st.expander("Filtros", expanded=False):
            min_len = st.slider("Minimum length (runes)", 0, int(df_feat["Length"].max()), 0)
            df_feat = df_feat[df_feat["Length"] >= min_len]

        col1, col2 = st.columns(2)
        with col1:
                fig_ioc = px.scatter(
                df_feat, x="IoC", y="Entropy",
                color="Fitness", size="Length",
                hover_name="Page",
                    title="Crypto-Signature: Entropy vs IoC",
                color_continuous_scale="viridis",
            )
            
        fig_ioc.add_vline(x=0.034, line_dash="dash", line_color="red",   annotation_text="Random")
        fig_ioc.add_vline(x=0.067, line_dash="dash", line_color="green",  annotation_text="Plaintext")
        st.plotly_chart(fig_ioc, width="stretch")

        with col2:
            fig_fit = px.bar(
                df_feat.sort_values("Fitness", ascending=False),
                x="Page", y="Fitness", color="Fitness",
                title="Advanced Fitness Ranking",
                color_continuous_scale="viridis",
            )
            st.plotly_chart(fig_fit, width="stretch")

        # Colunas adicionais (quando disponíveis)
        extra_cols = [c for c in ["ChiSquare", "BigramIoC", "GpsSumMean", "TopDelta", "LRS", "NumInterrupts"]
                      if c in df_feat.columns]
        if extra_cols:
            st.subheader("Additional Metrics")
            metric_sel = st.selectbox("Metric for ranking", extra_cols)
            fig_extra  = px.bar(
                df_feat.sort_values(metric_sel, ascending=False),
                x="Page", y=metric_sel, color=metric_sel,
                title=f"Ranking por {metric_sel}",
                color_continuous_scale="plasma",
            )
            st.plotly_chart(fig_extra, width="stretch")

        st.subheader("Full table")
        st.dataframe(
            df_feat.sort_values("Fitness", ascending=False)
                   .style.background_gradient(
                       subset=[c for c in ["IoC", "Entropy", "Fitness"] if c in df_feat.columns],
                       cmap="viridis",
                   ),
            width="stretch",
        )

# ── Tab 3: Geometria de Interrupções ─────────────────────────────────────────
with tab_geom:
    st.header("Interrupt Deltas & Geometry")

    delta_path = os.path.join(OUTPUT_DIR, "interrupt_deltas.csv")
    if not os.path.exists(delta_path):
        st.warning("interrupt_deltas.csv not found. Run Option 6 in the C++ tool.")
    else:
        stale_warning(delta_path)
        df_delta = load_csv(delta_path)

        fig_hist = px.histogram(
            df_delta, x="Delta", nbins=50,
            title="Distribution of Interrupt Intervals",
        )
        st.plotly_chart(fig_hist, width="stretch")

        col1, col2 = st.columns(2)
        with col1:
            st.subheader("Top Deltas por frequência")
            top_d = df_delta["Delta"].value_counts().head(15).reset_index()
            top_d.columns = ["Delta", "Count"]
            fig_top = px.bar(top_d, x="Delta", y="Count", title="Top 15 Deltas")
            st.plotly_chart(fig_top, width="stretch")

        with col2:
            if "Page" in df_delta.columns and "TopDelta" in df_delta.columns:
                st.subheader("Top Delta per Page")
                top_page = df_delta.sort_values("TopDelta", ascending=False).drop_duplicates("Page")
                fig_td = px.bar(
                    top_page, x="Page", y="TopDelta",
                    title="Largest interval per page",
                    color="TopDelta", color_continuous_scale="reds",
                )
                st.plotly_chart(fig_td, width="stretch")
            else:
                st.info("TopDelta column not available in this file.")

        # Imagens do histograma gerado pelo script Python
        hist_img = delta_path.replace(".csv", "_histogram.png")
        if os.path.exists(hist_img):
            st.image(hist_img, caption="Full histogram generated by visualize_correlations.py", width="stretch")

# ── Tab 4: Key Stream ────────────────────────────────────────────────────────
with tab_ks:
    st.header("Periodic Analysis of Key Stream")

    ks_path = os.path.join(OUTPUT_DIR, "key_stream_analysis.csv")
    if not os.path.exists(ks_path):
        st.warning("key_stream_analysis.csv not found. Run Option 5 in the C++ tool.")
    else:
        stale_warning(ks_path)
        df_ks    = load_csv(ks_path)
        pages_ks = df_ks["Page"].unique().tolist()
        page_sel = st.selectbox("Select a resolved page", pages_ks)
        df_pg    = df_ks[df_ks["Page"] == page_sel]

        fig_ks = px.line(
            df_pg, x="Position", y="KeyValue",
            title=f"Key Stream: {page_sel}",
        )
        st.plotly_chart(fig_ks, width="stretch")

        # FFT
        stream = df_pg["KeyValue"].values.astype(float)
        if len(stream) >= 10:
            fft_vals    = np.abs(np.fft.rfft(stream - stream.mean()))
            freqs       = np.fft.rfftfreq(len(stream))
            fft_vals[0] = 0
            fig_fft = go.Figure()
            fig_fft.add_trace(go.Scatter(
                x=freqs[1:], y=fft_vals[1:], mode="lines",
                line=dict(color="darkorange"), name="FFT",
            ))
            dom_idx = int(np.argmax(fft_vals[1:])) + 1
            if dom_idx < len(freqs) and freqs[dom_idx] > 0:
                period = int(round(1.0 / freqs[dom_idx]))
                fig_fft.add_vline(
                    x=freqs[dom_idx], line_dash="dash", line_color="red",
                    annotation_text=f"Período: {period}",
                )
            fig_fft.update_layout(title=f"Key Stream FFT: {page_sel}",
                                  xaxis_title="Frequency", yaxis_title="Magnitude")
            st.plotly_chart(fig_fft, width="stretch")

        fft_img = ks_path.replace(".csv", "_fft_autocorr.png")
        if os.path.exists(fft_img):
            st.image(fft_img, caption="FFT + Autocorrelation (all resolved pages)", width="stretch")

    st.info("Strong periodicity in the key stream indicates a predictable mathematical algorithm.")

# ── Tab 5: Suspeição ─────────────────────────────────────────────────────────
with tab_susp:
    st.header("Intelligent Suspicion Ranking")

    report_path = os.path.join(OUTPUT_DIR, "corr_runic_report.txt")
    feat_path2  = os.path.join(OUTPUT_DIR, "page_features.csv")

    if os.path.exists(report_path):
        stale_warning(report_path)
        with open(report_path, "r", encoding="utf-8") as f:
            content = f.read()

        # Extrai a seção de suspeição
        if "=== TOP SUSPICIOUS PAGES (PRIORITY SCORE) ===" in content:
            sus_section = content.split("=== TOP SUSPICIOUS PAGES (PRIORITY SCORE) ===")[-1]
            sus_text    = sus_section.split("---")[0].strip()

            # Parseia as linhas do ranking para um DataFrame interativo
            rows = []
            for line in sus_text.splitlines():
                m = __import__("re").match(r"(\S+)\s+->\s+Score:\s+([\d.]+)", line.strip())
                if m:
                    rows.append({"Page": m.group(1), "Score": float(m.group(2))})

            if rows:
                df_susp = pd.DataFrame(rows)
                fig_susp = px.bar(
                    df_susp, x="Page", y="Score", color="Score",
                    title="Suspicion Score by Page (multi-factor)",
                    color_continuous_scale="reds",
                )
                st.plotly_chart(fig_susp, width="stretch")

        st.subheader("Full report (corr_runic_report.txt)")
        with st.expander("View report"):
            st.text(content)
    else:
        st.warning("corr_runic_report.txt not found. Run `visualize_correlations.py` after C++ Option 1.")

    if os.path.exists(feat_path2):
        df_feat2 = load_csv(feat_path2)
        st.subheader("Highlighted Features Table")
        highlight_cols = [c for c in ["IoC", "Entropy", "Fitness"] if c in df_feat2.columns]
        st.dataframe(
            df_feat2.sort_values("Fitness", ascending=False)
                    .style.background_gradient(subset=highlight_cols, cmap="viridis"),
            width="stretch",
        )

# ── Tab 6: Heurísticas ───────────────────────────────────────────────────────
with tab_heur:
    st.header("Cipher Heuristics per Page")

    heur_path = os.path.join(OUTPUT_DIR, "heuristic_scores.csv")
    if not os.path.exists(heur_path):
        st.warning("heuristic_scores.csv not found. Run Option 9 in the C++ tool.")
    else:
        stale_warning(heur_path)
        df_heur = load_csv(heur_path)

        col1, col2 = st.columns(2)
        with col1:
            fig_hf = px.bar(
                df_heur.sort_values("Fitness", ascending=False),
                x="Page", y="Fitness", color="BestCipher",
                title="Fitness by Page (colored by cipher)",
            )
            st.plotly_chart(fig_hf, width="stretch")

        with col2:
            if "IoC" in df_heur.columns:
                fig_hi = px.scatter(
                    df_heur, x="IoC", y="Fitness",
                    color="BestCipher", hover_name="Page",
                    title="IoC vs Fitness — Attack map",
                )
                fig_hi.add_vline(x=0.034, line_dash="dash", line_color="gray")
                fig_hi.add_vline(x=0.067, line_dash="dash", line_color="green")
                st.plotly_chart(fig_hi, width="stretch")

        if "BestVigenereKeyLen" in df_heur.columns:
            vig = df_heur[df_heur["BestVigenereKeyLen"] > 0].sort_values("BestVigenereKeyLen")
            if not vig.empty:
                fig_vig = px.bar(
                    vig, x="Page", y="BestVigenereKeyLen",
                    title="Pages with detected Vigenère key length",
                )
                st.plotly_chart(fig_vig, width="stretch")

        st.dataframe(df_heur, width="stretch")

# ── Tab 7: Friedman & Kasiski ────────────────────────────────────────────────
with tab_fk:
    st.header("Key Length Analysis")

    friedman_path = os.path.join(OUTPUT_DIR, "friedman_scan.csv")
    kasiski_path  = os.path.join(OUTPUT_DIR, "kasiski_results.txt")

    col1, col2 = st.columns(2)

    with col1:
        st.subheader("Friedman scan (Periodic IoC)")
        if os.path.exists(friedman_path):
            stale_warning(friedman_path)
            df_fr = load_csv(friedman_path)
            if {"Page", "KeyLen", "PeriodicIoC"}.issubset(df_fr.columns):
                pivot = df_fr.pivot(index="Page", columns="KeyLen", values="PeriodicIoC")
                fig_fr = px.imshow(
                    pivot, aspect="auto", color_continuous_scale="YlOrRd",
                    title="Heatmap Friedman: IoC Periódico por KeyLen",
                )
                st.plotly_chart(fig_fr, width="stretch")
            else:
                st.dataframe(df_fr, width="stretch")
        else:
            st.warning("friedman_scan.csv not found. Run Option 20 in the C++ tool (internal).")

    with col2:
        st.subheader("Kasiski Examination (GCDs)")
        if os.path.exists(kasiski_path):
            stale_warning(kasiski_path)
            import re as _re
            gcds = []
            with open(kasiski_path, "r", encoding="utf-8") as f:
                for line in f:
                    m = _re.search(r"GCD of deltas:\s*(\d+)", line)
                    if m:
                        v = int(m.group(1))
                        if v > 1:
                            gcds.append(v)

            if gcds:
                from collections import Counter
                gcd_counts = Counter(gcds)
                df_gcd = pd.DataFrame(
                    sorted(gcd_counts.items()), columns=["GCD", "Occurrences"]
                )
                fig_gcd = px.bar(
                    df_gcd, x="GCD", y="Occurrences",
                    title="Distribuição de GCDs (candidatos a comprimento de chave)",
                    color="Occurrences", color_continuous_scale="blues",
                )
                fig_gcd.update_layout(yaxis_title="Occurrences")
                st.plotly_chart(fig_gcd, width="stretch")
                st.caption(f"Total patterns found: {len(gcds)}")
            else:
                with open(kasiski_path, "r", encoding="utf-8") as f:
                    st.text(f.read()[:3000])
        else:
            st.warning("kasiski_results.txt não encontrado. Execute a Opção 15 no C++.")

    # Cluster Mutual IoC
    cluster_path = os.path.join(OUTPUT_DIR, "cluster_mutual_ioc.csv")
    if os.path.exists(cluster_path):
        st.subheader("Cluster Mutual IoC")
        stale_warning(cluster_path)
        df_cl  = load_csv_index(cluster_path)
        fig_cl = px.imshow(
            df_cl, aspect="auto", color_continuous_scale="Blues",
            title="Mutual IoC entre Páginas do Cluster",
        )
        st.plotly_chart(fig_cl, width="stretch")

    # Rolling IoC
    rolling_path = os.path.join(OUTPUT_DIR, "rolling_ioc_analysis.txt")
    rolling_dir  = os.path.join(OUTPUT_DIR, "rolling_plots")
    if os.path.exists(rolling_dir):
        st.subheader("Rolling IoC (Sliding Window)")
        import glob
        rolling_imgs = sorted(glob.glob(os.path.join(rolling_dir, "ioc_*.png")))
        if rolling_imgs:
            sel_img = st.selectbox(
                "Select page",
                rolling_imgs,
                format_func=lambda p: os.path.basename(p).replace("ioc_", "").replace(".png", ""),
            )
            st.image(sel_img, width="stretch")

# ── Tab 8: Unigrama & Zipf ───────────────────────────────────────────────────
with tab_uni:
    st.header("Unigram Distribution & Zipf Law")

    uni_path = os.path.join(OUTPUT_DIR, "liber_unigram_target.csv")
    if not os.path.exists(uni_path):
        st.warning("liber_unigram_target.csv not found. Run Option 1 in the C++ tool.")
    else:
        stale_warning(uni_path)
        df_uni = load_csv(uni_path)

        col1, col2 = st.columns(2)
        with col1:
            fig_uni = px.bar(
                df_uni, x="RuneIndex", y="Frequency",
                title="Unigram Frequency of Resolved Pages",
                color="Frequency", color_continuous_scale="magma",
            )
            st.plotly_chart(fig_uni, width="stretch")

        with col2:
            freqs_sorted = np.sort(df_uni["Frequency"].values)[::-1]
            ranks        = np.arange(1, len(freqs_sorted) + 1)
            fig_zipf     = go.Figure()
            fig_zipf.add_trace(go.Scatter(
                x=np.log(ranks), y=np.log(freqs_sorted + 1e-12),
                mode="markers+lines", name="Liber Primus",
            ))
            ideal = np.log(freqs_sorted[0]) - np.log(ranks)
            fig_zipf.add_trace(go.Scatter(
                x=np.log(ranks), y=ideal,
                mode="lines", name="Zipf ideal (s=1)",
                line=dict(dash="dash", color="gray"),
            ))
            fig_zipf.update_layout(
                title="Zipf Analysis (log-log scale)",
                xaxis_title="log(Rank)", yaxis_title="log(Frequency)",
            )
            st.plotly_chart(fig_zipf, width="stretch")

        # Imagens geradas pelo script
        imgs_uni = {
            "Distribuição (barras)": uni_path.replace(".csv", "_dist.png"),
            "Zipf (matplotlib)":     uni_path.replace(".csv", "_zipf.png"),
        }
        for label, img in imgs_uni.items():
            if os.path.exists(img):
                st.image(img, caption=label, width="stretch")

# ── Tab 9: Resumo Mestre ──────────────────────────────────────────────────────
with tab_master:
    st.header("Master Research Summary")

    master_path = os.path.join(OUTPUT_DIR, "MASTER_RESEARCH_SUMMARY.log")
    if os.path.exists(master_path):
        with open(master_path, "r", encoding="utf-8") as f:
            content = f.read()
        st.text_area("MASTER_RESEARCH_SUMMARY.log content", content, height=600)
        st.download_button(
            "⬇️ Download summary",
            data=content.encode("utf-8"),
            file_name="MASTER_RESEARCH_SUMMARY.log",
            mime="text/plain",
        )
    else:
        st.warning(
            "MASTER_RESEARCH_SUMMARY.log not found. "
            "Run `visualize_correlations.py` to generate it."
        )

    # Relatórios individuais por tipo de ataque
    import glob as _glob
    txt_reports = _glob.glob(os.path.join(OUTPUT_DIR, "*.txt"))
    if txt_reports:
        st.subheader("Other .txt reports")
        report_sel = st.selectbox(
            "Select report",
            txt_reports,
            format_func=lambda p: os.path.basename(p),
        )
        if report_sel:
            try:
                with open(report_sel, "r", encoding="utf-8") as f:
                    st.text_area("Content", f.read(), height=400)
            except Exception as exc:
                st.error(f"Error reading {report_sel}: {exc}")

# ── Tab: Delta Stream & Autocorrelation (NEW) ──────────────────────────────
with tab_delta:
    st.header("🌊 Delta Stream Analysis (C[i] - C[i-1])")
    st.markdown("Delta Stream removes constant key offsets (e.g. Caesar), revealing PRNGs or hidden periodic streams.")

    delta_csv = os.path.join(OUTPUT_DIR, "delta_stream_analysis.csv")
    autocorr_csv = os.path.join(OUTPUT_DIR, "delta_autocorrelation.csv")

    if os.path.exists(delta_csv) and os.path.exists(autocorr_csv):
        df_ds = load_csv(delta_csv)
        df_ac = load_csv(autocorr_csv)

        pages = df_ds["Page"].unique().tolist()
        page_sel = st.selectbox("Select page for Delta analysis", pages)

        df_ds_pg = df_ds[df_ds["Page"] == page_sel]
        df_ac_pg = df_ac[df_ac["Page"] == page_sel]

        col1, col2 = st.columns(2)

        with col1:
            # Histogram of Delta frequencies (0 to 28)
            fig_ds_hist = px.histogram(
                df_ds_pg, x="Delta", nbins=29,
                title=f"Delta Frequency (0-28): {page_sel}",
                color_discrete_sequence=["#9467bd"]
            )
            fig_ds_hist.update_xaxes(tickmode='linear', dtick=1)
            st.plotly_chart(fig_ds_hist, width="stretch")

        with col2:
            # Delta Autocorrelation plot
            fig_ac = px.line(
                df_ac_pg, x="Lag", y="Score", markers=True,
                title=f"Delta Autocorrelation (Period search): {page_sel}",
                color_discrete_sequence=["#d62728"]
            )

            # Add vertical lines every multiple of 29
            max_lag = df_ac_pg["Lag"].max() if not df_ac_pg.empty else 0
            for mult in range(29, int(max_lag) + 1, 29):
                fig_ac.add_vline(x=mult, line_dash="dash", line_color="orange", opacity=0.5)

            st.plotly_chart(fig_ac, width="stretch")

        st.subheader("Raw Delta Stream Sequence")
        fig_ds_seq = px.scatter(
            df_ds_pg, x="Position", y="Delta",
            title="Delta Stream Values Sequence",
            color_discrete_sequence=["#1f77b4"], opacity=0.6
        )
        st.plotly_chart(fig_ds_seq, width="stretch")
    else:
        st.warning("Delta Stream files not found. Run Options 16 and 17 in the C++ tool.")

# ── Tab: Global Clusters (NOVA) ─────────────────────────────────────────────
with tab_gcluster:
    st.header("🧩 Global Cluster Analysis (Merged Pages)")
    st.markdown("Treating highly inter-connected pages as a single document to mitigate cipher fragmentation.")

    cluster_txt = os.path.join(OUTPUT_DIR, "cluster_analysis.txt")
    if os.path.exists(cluster_txt):
        stale_warning(cluster_txt)
        with open(cluster_txt, "r", encoding="utf-8") as f:
            cluster_content = f.read()

        # Expressão regular ou visualizador em expanders para o log de texto
        clusters_blocks = cluster_content.split("==========================================")
        
        for block in clusters_blocks:
            if not block.strip():
                continue
            
            # Extrai o nome do cluster (ex: --- Cluster_D_Pages_33_to_39 ---)
            title_match = __import__("re").search(r"---\s*(Cluster_.*?)\s*---", block)
            c_name = title_match.group(1) if title_match else "Analyzed Cluster"

            with st.expander(f"Analysis: {c_name}", expanded=True):
                st.text(block.strip())

                # Highlight if Friedman scan found peaks > 0.045
                if "KeyLen" in block:
                    st.success("🎯 Peaks detected in the global Friedman scan! Check KeyLens above.")

                # Highlight if Kasiski patterns exist
                if "GCD:" in block and "(No long pattern" not in block:
                    st.info("🔎 Kasiski found multi-page repeats in this cluster. Inspect the GCD.")
    else:
        st.warning("cluster_analysis.txt not found. Run Option 22 in the C++ tool.")