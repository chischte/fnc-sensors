"""
fastnchip-sensors – long-term data viewer
Reads measurements.csv and shows interactive charts for CO2, Temperature,
and Humidity. Refreshes automatically while the logger is running.

Requirements:
    pip install matplotlib pandas
"""

import sys
import sqlite3
from contextlib import closing
from datetime import timedelta
from pathlib import Path

if __package__:
    from .viewer_window import configure_viewer_window
else:
    from viewer_window import configure_viewer_window

try:
    import pandas as pd
    import matplotlib.pyplot as plt
    import matplotlib.dates as mdates
    import matplotlib.patches as mpatches
    from matplotlib.ticker import AutoLocator, FixedLocator, MultipleLocator
    from matplotlib.patches import FancyBboxPatch
    import matplotlib.animation as animation
    from matplotlib.widgets import Button
except ImportError:
    print("Missing dependencies. Run:  pip install matplotlib pandas")
    sys.exit(1)

CSV_FILE = Path(__file__).parent / "data" / "measurements.csv"
DB_FILE = Path(__file__).parent / "data" / "measurements.db"
REFRESH_INTERVAL_MS = 10_000
AXIS_DATE_FORMAT = "%d/%m/%y"
AXIS_LABEL_FONT_SIZE = 8

LIGHT_THEME = {
    "zoom_selection": "#007acc",
    "page": "#f3f6f4",
    "card": "#ffffff",
    "plot": "#f9faf9",
    "grid": "#d9e2dc",
    "text": "#2a3630",
    "muted": "#607068",
    "track": "#9aada6",
    "selected": "#3b8c62",
    "button_on": "#638372",
    "button_off": "#cdd3d0",
    "button_text_on": "#ffffff",
    "button_text_off": "#46504b",
}
DARK_THEME = {
    "zoom_selection": "#7ce8ff",
    "page": "#171e1b",
    "card": "#242e29",
    "plot": "#1d2621",
    "grid": "#3c4b43",
    "text": "#e6eee9",
    "muted": "#adbbb3",
    "track": "#778e81",
    "selected": "#78c79b",
    "button_on": "#638372",
    "button_off": "#3c4b43",
    "button_text_on": "#ffffff",
    "button_text_off": "#d3dfd7",
}
ADAPTIVE_BUTTON_CENTER_X = 0.825
DARK_MODE_BUTTON_CENTER_X = 0.175
TOGGLE_BUTTON_CENTER_Y = 0.0585
TOGGLE_BUTTON_WIDTH_INCHES = 1.69
TOGGLE_BUTTON_HEIGHT_INCHES = 0.37
CONTROL_ROW_TOP = 0.09
X_AXIS_LABEL_SPACE_INCHES = 0.45
CONTROL_BUTTON_GAP_INCHES = 0.10


def current_theme() -> dict:
    return DARK_THEME if _state["dark_mode"] else LIGHT_THEME


SERIES = [
    {"col": "humidity_corrected_rh", "label": "Humidity",    "unit": "%RH", "color": "#2878a8", "ymin": 85, "ymax": 100,   "step": 5,    "fmt": ".1f",
     "line_label": "SHT45", "overlay_col": "humidity_scd_rh",
     "overlay_color": "#78a9c4", "overlay_label": "SCD41", "overlay_style": ":"},
    {"col": "co2_ppm",     "label": "CO2",        "unit": "ppm", "color": "#7b2cbf", "ymin": 0,  "ymax": 10000, "step": None, "fmt": ".0f"},
    {"col": "temp_box_c",  "label": "Temperature", "unit": "°C",  "color": "#d65a4a", "ymin": 20, "ymax": 30,    "step": 2,    "fmt": ".1f",
     "line_label": "Inbox (SHT45)", "ticks": [20, 22, 24, 26, 28, 30],
     "overlay_col": "temp_outer_c", "overlay_color": "#e89489"},
]

_state = {
    "range": "24h",
    "adaptive_y": False,
    "dark_mode": False,
    "manual_view": False,
}

RANGES = [
    ("all", "∞",      None),
    ("40d", "40d",    timedelta(days=40)),
    ("1w", "1w",      timedelta(weeks=1)),
    ("24h", "24h",    timedelta(hours=24)),
    ("3h", "3h",      timedelta(hours=3)),
    ("30m", "30min",  timedelta(minutes=30)),
    ("5m", "5min",    timedelta(minutes=5)),
]

RANGE_TRACK_START = 0.12
RANGE_TRACK_END = 0.88
RANGE_MINUS_X = 0.04
RANGE_PLUS_X = 0.96


# ---------------------------------------------------------------------------
# Data helpers
# ---------------------------------------------------------------------------

def load_data() -> pd.DataFrame:
    if DB_FILE.exists():
        with closing(sqlite3.connect(DB_FILE, timeout=10)) as db:
            columns = {row[1] for row in db.execute("PRAGMA table_info(measurements)")}
            optional_columns = ",".join(
                name if name in columns else f"NULL AS {name}"
                for name in ("humidity_scd_rh", "humidity_offset_rh"))
            df = pd.read_sql_query(
                f"""SELECT COALESCE(device_time,received_at) AS timestamp,
                   co2_ppm,temp_box_c,humidity_rh,temp_outer_c,{optional_columns}
                   FROM measurements
                   ORDER BY id""", db)
    else:
        df = pd.read_csv(CSV_FILE,
                         usecols=lambda name: name in {"timestamp", "co2_ppm", "temp_box_c",
                                                      "humidity_rh", "temp_outer_c", "humidity_scd_rh", "humidity_offset_rh"},
                         on_bad_lines="skip")
    # Legacy CSV rows contain local timestamps without an offset. New rows are
    # ISO-8601 timestamps with an offset. Preserve the legacy wall-clock time
    # and convert offset timestamps to Zurich wall-clock time before sorting.
    raw_timestamp = df["timestamp"].astype("string")
    has_offset = raw_timestamp.str.contains(r"(?:Z|[+-]\d{2}:\d{2})$", na=False)
    timestamp = pd.Series(pd.NaT, index=df.index, dtype="datetime64[ns]")
    timestamp.loc[~has_offset] = pd.to_datetime(
        raw_timestamp.loc[~has_offset], format="mixed", errors="coerce"
    )
    timestamp.loc[has_offset] = (
        pd.to_datetime(raw_timestamp.loc[has_offset], format="mixed", errors="coerce", utc=True)
        .dt.tz_convert("Europe/Zurich")
        .dt.tz_localize(None)
    )
    df["timestamp"] = timestamp
    df.dropna(subset=["timestamp"], inplace=True)
    if "temp_outer_c" not in df.columns:
        df["temp_outer_c"] = float("nan")
    df["temp_outer_c"] = pd.to_numeric(df["temp_outer_c"], errors="coerce")
    for column in ("humidity_scd_rh", "humidity_offset_rh"):
        if column not in df.columns:
            df[column] = float("nan")
        df[column] = pd.to_numeric(df[column], errors="coerce")
    df["humidity_corrected_rh"] = (
        pd.to_numeric(df["humidity_rh"], errors="coerce")
        + df["humidity_offset_rh"].fillna(0)
    ).clip(0, 100)
    df.sort_values("timestamp", inplace=True)
    return df


def filter_data(df: pd.DataFrame) -> pd.DataFrame:
    delta = next(d for k, _, d in RANGES if k == _state["range"])
    if delta is None:
        return df
    cutoff = df["timestamp"].max() - delta
    return df[df["timestamp"] >= cutoff]


# ---------------------------------------------------------------------------
# Chart helpers
# ---------------------------------------------------------------------------

def format_xaxis(ax: "plt.Axes", df: pd.DataFrame) -> None:
    span = (df["timestamp"].max() - df["timestamp"].min()).total_seconds()

    def _fmt_with_midnight(x, pos):
        dt = mdates.num2date(x)
        if dt.hour == 0 and dt.minute == 0:
            return dt.strftime(f"{AXIS_DATE_FORMAT}\n00:00")
        return dt.strftime("%H:%M")

    def _bold_midnight():
        ax.get_figure().canvas.draw()
        for label in ax.get_xticklabels():
            if "\n" in label.get_text():
                label.set_fontweight("bold")

    if _state["range"] == "1w":
        ax.xaxis.set_major_locator(mdates.HourLocator(byhour=[0, 12]))
        ax.xaxis.set_major_formatter(plt.FuncFormatter(_fmt_with_midnight))
        t_max = df["timestamp"].max()
        ax.set_xlim(mdates.date2num(t_max - timedelta(weeks=1)), mdates.date2num(t_max))
        plt.setp(ax.get_xticklabels(), rotation=30, ha="right")
        _bold_midnight()
    elif _state["range"] == "24h":
        ax.xaxis.set_major_locator(mdates.HourLocator(interval=1))
        ax.xaxis.set_major_formatter(plt.FuncFormatter(_fmt_with_midnight))
        t_max = df["timestamp"].max()
        ax.set_xlim(mdates.date2num(t_max - timedelta(hours=24)), mdates.date2num(t_max))
        plt.setp(ax.get_xticklabels(), rotation=30, ha="right")
        _bold_midnight()
    elif _state["range"] == "3h":
        ax.xaxis.set_major_locator(mdates.MinuteLocator(byminute=[0, 30]))
        ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
        t_max = df["timestamp"].max()
        ax.set_xlim(mdates.date2num(t_max - timedelta(hours=3)), mdates.date2num(t_max))
        plt.setp(ax.get_xticklabels(), rotation=30, ha="right")
    elif _state["range"] == "5m":
        ax.xaxis.set_major_locator(mdates.MinuteLocator(interval=1))
        ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
        t_max = df["timestamp"].max()
        ax.set_xlim(mdates.date2num(t_max - timedelta(minutes=5)), mdates.date2num(t_max))
        plt.setp(ax.get_xticklabels(), rotation=30, ha="right")
    elif _state["range"] == "30m":
        ax.xaxis.set_major_locator(mdates.MinuteLocator(byminute=range(0, 60, 5)))
        ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
        t_max = df["timestamp"].max()
        ax.set_xlim(mdates.date2num(t_max - timedelta(minutes=30)), mdates.date2num(t_max))
        plt.setp(ax.get_xticklabels(), rotation=30, ha="right")
    elif span < 3600:
        ax.xaxis.set_major_locator(mdates.MinuteLocator(byminute=range(0, 60, 5)))
        ax.xaxis.set_major_formatter(mdates.DateFormatter("%H:%M"))
        plt.setp(ax.get_xticklabels(), rotation=30, ha="right")
    elif span < 86400:
        ax.xaxis.set_major_locator(mdates.HourLocator())
        ax.xaxis.set_major_formatter(plt.FuncFormatter(_fmt_with_midnight))
        plt.setp(ax.get_xticklabels(), rotation=30, ha="right")
        _bold_midnight()
    elif span < 7 * 86400:
        ax.xaxis.set_major_locator(mdates.HourLocator(byhour=range(0, 24, 6)))
        ax.xaxis.set_major_formatter(plt.FuncFormatter(_fmt_with_midnight))
        plt.setp(ax.get_xticklabels(), rotation=30, ha="right")
        _bold_midnight()
    else:
        ax.xaxis.set_major_locator(mdates.DayLocator())
        ax.xaxis.set_major_formatter(mdates.DateFormatter(AXIS_DATE_FORMAT))
        plt.setp(ax.get_xticklabels(), rotation=30, ha="right")


def format_zoomed_xaxis(ax: "plt.Axes") -> None:
    locator = mdates.AutoDateLocator(
        minticks=5,
        maxticks=10,
        interval_multiples=True,
    )
    ax.xaxis.set_major_locator(locator)
    ax.xaxis.set_major_formatter(mdates.ConciseDateFormatter(
        locator,
        formats=[AXIS_DATE_FORMAT, AXIS_DATE_FORMAT, "%d", "%H:%M", "%H:%M", "%S.%f"],
        zero_formats=["", AXIS_DATE_FORMAT, AXIS_DATE_FORMAT, AXIS_DATE_FORMAT, "%H:%M", "%H:%M"],
        offset_formats=[
            "", AXIS_DATE_FORMAT, AXIS_DATE_FORMAT, AXIS_DATE_FORMAT,
            AXIS_DATE_FORMAT, f"{AXIS_DATE_FORMAT} %H:%M",
        ],
    ))
    ax.xaxis.get_offset_text().set_fontsize(AXIS_LABEL_FONT_SIZE)
    plt.setp(ax.get_xticklabels(), rotation=30, ha="right")


def set_y_axis(ax: "plt.Axes", series: dict) -> None:
    if not _state["adaptive_y"]:
        ax.set_ylim(series["ymin"], series["ymax"])
        if "ticks" in series:
            locator = FixedLocator(series["ticks"])
        else:
            locator = MultipleLocator(series["step"]) if series["step"] else AutoLocator()
        ax.yaxis.set_major_locator(locator)
        return

    x_min, x_max = sorted(ax.get_xlim())
    visible_values = []
    for line in ax.lines:
        x_values = line.get_xdata(orig=False)
        y_values = line.get_ydata(orig=False)
        visible = (
            pd.notna(x_values)
            & pd.notna(y_values)
            & (x_values >= x_min)
            & (x_values <= x_max)
        )
        visible_values.extend(y_values[visible])

    if not visible_values:
        return

    visible_min = min(visible_values)
    visible_max = max(visible_values)
    visible_span = visible_max - visible_min
    padding = visible_span * 0.08 if visible_span else max(abs(visible_min) * 0.05, 0.5)
    ax.set_ylim(visible_min - padding, visible_max + padding)
    ax.yaxis.set_major_locator(AutoLocator())


def synchronize_x_axes(chart_axes: list, source_axis: "plt.Axes") -> None:
    x_limits = source_axis.get_xlim()
    for axis in chart_axes:
        if axis is not source_axis:
            axis.set_xlim(x_limits)


def position_toggle_button(fig: "plt.Figure", button_axis: "plt.Axes",
                           center_x: float) -> None:
    width = TOGGLE_BUTTON_WIDTH_INCHES / fig.get_figwidth()
    height = TOGGLE_BUTTON_HEIGHT_INCHES / fig.get_figheight()
    button_axis.set_position([
        center_x - width / 2,
        TOGGLE_BUTTON_CENTER_Y - height / 2,
        width,
        height,
    ])


def style_toggle_button(button: Button, background: FancyBboxPatch,
                        label: str, enabled: bool) -> None:
    theme = current_theme()
    button.color = theme["page"]
    button.hovercolor = theme["page"]
    button.ax.set_facecolor(theme["page"])
    button.label.set_text(f"{label}: {'on' if enabled else 'off'}")
    button.label.set_color(theme["button_text_on" if enabled else "button_text_off"])
    background.set_facecolor(theme["button_on" if enabled else "button_off"])


def create_toggle_button(fig: "plt.Figure", center_x: float,
                         label: str, enabled: bool) -> tuple[Button, FancyBboxPatch]:
    button_axis = fig.add_axes([0, 0, 0.13, 0.035])
    position_toggle_button(fig, button_axis, center_x)
    button = Button(button_axis, "")
    for spine in button_axis.spines.values():
        spine.set_visible(False)
    background = FancyBboxPatch(
        (0, 0), 1, 1,
        boxstyle="round,pad=0.02,rounding_size=0.24",
        linewidth=0, transform=button_axis.transAxes, zorder=1,
    )
    button_axis.add_patch(background)
    button.label.set_fontsize(9.5)
    button.label.set_fontweight("bold")
    button.label.set_zorder(2)
    style_toggle_button(button, background, label, enabled)
    return button, background


def plot_series(ax: "plt.Axes", df: pd.DataFrame, s: dict) -> None:
    theme = current_theme()
    ax.clear()
    col = s["col"]
    ax.fill_between(df["timestamp"], df[col], s["ymin"],
                    color=s["color"], alpha=0.12, zorder=2)
    ax.plot(df["timestamp"], df[col], color=s["color"], linewidth=1.5, zorder=3,
            label=s.get("line_label", "Box"))

    overlays = list(s.get("extra_overlays", []))
    if s.get("overlay_col"):
        overlays.insert(0, {"col": s["overlay_col"], "color": s["overlay_color"],
                            "style": s.get("overlay_style", "--"),
                            "label": s.get("overlay_label", "Ambient (PT100)")})
    for overlay in overlays:
        if overlay["col"] not in df.columns:
            continue
        ax.plot(df["timestamp"], df[overlay["col"]], color=overlay["color"],
                linewidth=1.2, linestyle=overlay["style"], zorder=4,
                label=overlay["label"])
    if overlays:
        ax.legend(fontsize=7, loc="upper left", framealpha=0.7,
                  facecolor=theme["card"], edgecolor=theme["grid"],
                  labelcolor=theme["text"])

    ax.set_title(f'{s["label"]} [{s["unit"]}]', fontsize=10, fontweight="bold",
                 color=theme["text"], pad=6)
    ax.set_facecolor(theme["plot"])
    ax.spines[["top", "right"]].set_visible(False)
    ax.spines[["left", "bottom"]].set_color(theme["grid"])
    ax.tick_params(colors=theme["muted"], labelsize=AXIS_LABEL_FONT_SIZE)
    ax.xaxis.get_offset_text().set_color(theme["muted"])
    ax.xaxis.get_offset_text().set_fontsize(AXIS_LABEL_FONT_SIZE)
    ax.yaxis.get_offset_text().set_color(theme["muted"])
    ax.grid(True, linestyle="--", linewidth=0.6, color=theme["grid"], zorder=1)
    format_xaxis(ax, df)
    set_y_axis(ax, s)


# ---------------------------------------------------------------------------
# Value boxes (like the website cards)
# ---------------------------------------------------------------------------

def draw_value_boxes(box_axes: list, last: pd.Series) -> None:
    theme = current_theme()
    for ax, s in zip(box_axes, SERIES):
        ax.clear()
        ax.set_xlim(0, 1)
        ax.set_ylim(0, 1)
        ax.axis("off")

        # Card background with border
        card = FancyBboxPatch((0.03, 0.05), 0.94, 0.90,
                              boxstyle="round,pad=0.02",
                              linewidth=1, edgecolor=theme["grid"],
                              facecolor=theme["card"],
                              transform=ax.transAxes, zorder=1,
                              clip_on=False)
        ax.add_patch(card)

        val = last[s["col"]]
        val_str = format(val, s["fmt"])

        # Check for RTD overlay delta (temperature box only)
        overlay_col = s.get("overlay_col")
        has_delta = False
        delta_str = ""
        if s["col"] == "temp_box_c" and overlay_col in last.index:
            rtd_val = last[overlay_col]
            if pd.notna(rtd_val) and pd.notna(val):
                delta = val - rtd_val
                sign = "+" if delta >= 0 else ""
                delta_str = f"{sign}{delta:.1f}"
                has_delta = True

        val_y = 0.45

        # Label (top, bold, muted grey)
        ax.text(0.5, 0.78, s["label"],
                ha="center", va="center", fontsize=10, fontweight="bold",
                color=theme["muted"], transform=ax.transAxes, zorder=2)
        # Value (always centered in box)
        ax.text(0.5, val_y, val_str,
                ha="center", va="center", fontsize=22, fontweight="bold",
                color=s["color"], transform=ax.transAxes, zorder=2)
        # RTD delta (centered between value right-edge and box right wall)
        if has_delta:
            ax.text(0.82, val_y, delta_str,
                    ha="center", va="center", fontsize=11, fontweight="bold",
                    color=s.get("overlay_color", "#7bafd4"),
                    transform=ax.transAxes, zorder=2)
        # Unit (bottom)
        ax.text(0.5, 0.13, s["unit"],
                ha="center", va="center", fontsize=10, fontweight="bold",
                color=theme["muted"], transform=ax.transAxes, zorder=2)


# ---------------------------------------------------------------------------
# Range selector
# ---------------------------------------------------------------------------

def range_selector_positions() -> list[float]:
    spacing = (RANGE_TRACK_END - RANGE_TRACK_START) / (len(RANGES) - 1)
    return [RANGE_TRACK_START + index * spacing for index in range(len(RANGES))]


def draw_range_selector(range_axis: "plt.Axes") -> None:
    theme = current_theme()
    range_axis.clear()
    range_axis.set_xlim(0, 1)
    range_axis.set_ylim(0, 1)
    range_axis.axis("off")

    card = FancyBboxPatch((0.01, 0.08), 0.98, 0.84,
                          boxstyle="round,pad=0.02",
                          linewidth=1, edgecolor=theme["grid"],
                          facecolor=theme["card"],
                          transform=range_axis.transAxes, zorder=1,
                          clip_on=False)
    range_axis.add_patch(card)

    track_y = 0.60
    label_y = 0.22
    positions = range_selector_positions()
    range_axis.plot(
        [RANGE_TRACK_START, RANGE_TRACK_END], [track_y, track_y],
        color=theme["track"], linewidth=1.5,
        transform=range_axis.transAxes, zorder=2,
    )
    range_axis.text(
        RANGE_MINUS_X, track_y, "−",
        ha="center", va="center", fontsize=15, fontweight="bold",
        color=theme["button_text_off"], transform=range_axis.transAxes, zorder=3,
    )
    range_axis.text(
        RANGE_PLUS_X, track_y, "+",
        ha="center", va="center", fontsize=14, fontweight="bold",
        color=theme["button_text_off"], transform=range_axis.transAxes, zorder=3,
    )

    for position, (key, label, _) in zip(positions, RANGES):
        selected = _state["range"] == key
        marker_width = 0.075 if selected else 0.051
        marker_height = 0.100 if selected else 0.068
        marker = mpatches.Ellipse(
            (position, track_y),
            width=marker_width,
            height=marker_height,
            edgecolor=theme["selected"] if selected else theme["track"],
            facecolor=theme["selected"] if selected else theme["card"],
            linewidth=1.2, transform=range_axis.transAxes, zorder=3,
        )
        range_axis.add_patch(marker)
        range_axis.text(
            position, label_y, label,
            ha="center", va="center", fontsize=8,
            color=theme["text"] if selected else theme["muted"],
            fontweight="bold" if selected else "normal",
            transform=range_axis.transAxes, zorder=3,
        )

    range_axis.figure.canvas.draw_idle()


def range_index_for_click(x_position: float) -> int:
    current_index = next(
        index for index, (key, _, _) in enumerate(RANGES)
        if key == _state["range"]
    )
    if x_position <= (RANGE_MINUS_X + RANGE_TRACK_START) / 2:
        return max(0, current_index - 1)
    if x_position >= (RANGE_PLUS_X + RANGE_TRACK_END) / 2:
        return min(len(RANGES) - 1, current_index + 1)

    positions = range_selector_positions()
    return min(
        range(len(positions)),
        key=lambda index: abs(positions[index] - x_position),
    )


# ---------------------------------------------------------------------------
# Main draw loop
# ---------------------------------------------------------------------------

def draw(fig: "plt.Figure", box_axes: list, chart_axes: list,
         range_axis: "plt.Axes", footer_text: "plt.Text",
         preserve_view: bool = False) -> None:
    preserved_x_limits = (
        [axis.get_xlim() for axis in chart_axes] if preserve_view else None
    )

    try:
        df_all = load_data()
    except Exception as exc:
        print(f"Could not read CSV: {exc}", file=sys.stderr)
        return

    df = filter_data(df_all)
    if df.empty:
        return

    last = df_all.iloc[-1]
    ts = last["timestamp"]

    draw_value_boxes(box_axes, last)
    for ax, s in zip(chart_axes, SERIES):
        plot_series(ax, df, s)
    if preserved_x_limits:
        for axis, series, x_limits in zip(chart_axes, SERIES, preserved_x_limits):
            axis.set_xlim(x_limits)
            format_zoomed_xaxis(axis)
            set_y_axis(axis, series)
    draw_range_selector(range_axis)

    footer_text.set_text(
        f"Last Reading:  {ts.strftime(AXIS_DATE_FORMAT)}  {ts.strftime('%H:%M:%S')}"
        f"   |   {len(df_all)} data points"
    )

    fig.set_facecolor(current_theme()["page"])
    footer_text.set_color(current_theme()["muted"])
    fig.canvas.draw_idle()


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

def main() -> None:
    if not DB_FILE.exists() and not CSV_FILE.exists():
        print(f"No data file found at {DB_FILE}")
        print("Start logger.py first to collect measurements.")
        sys.exit(1)

    fig = plt.figure(figsize=(11, 8.5))
    fig.patch.set_facecolor(current_theme()["page"])
    window_controls = configure_viewer_window(fig)
    if window_controls is not None:
        window_controls.apply_theme(current_theme())

    from matplotlib.gridspec import GridSpec, GridSpecFromSubplotSpec

    # Outer grid: row 0 = boxes, row 1 = charts block, row 2 = toggle
    gs_outer = GridSpec(
        3, 1,
        figure=fig,
        height_ratios=[1, 6.5, 0.01],
        hspace=0.12,          # gap between boxes row and charts block
        top=0.97,
        bottom=CONTROL_ROW_TOP + X_AXIS_LABEL_SPACE_INCHES / fig.get_figheight(),
        left=0.07, right=0.97,
    )

    # Inner grid for the 3 charts — controls spacing between charts only
    gs_charts = GridSpecFromSubplotSpec(
        3, 1,
        subplot_spec=gs_outer[1],
        hspace=0.52,
    )

    # Value boxes (top row, one per column)
    gs_boxes = GridSpecFromSubplotSpec(1, 3, subplot_spec=gs_outer[0], wspace=0.28)
    box_axes = [fig.add_subplot(gs_boxes[0, i]) for i in range(3)]

    # Charts
    chart_axes = [fig.add_subplot(gs_charts[r]) for r in range(3)]

    # Range selector flanked by matching theme and adaptive Y toggles.
    range_axis = fig.add_axes([0.32, 0.030, 0.36, 0.060])
    adaptive_button, adaptive_button_background = create_toggle_button(
        fig, ADAPTIVE_BUTTON_CENTER_X, "adaptive y", _state["adaptive_y"]
    )
    dark_mode_button, dark_mode_button_background = create_toggle_button(
        fig, DARK_MODE_BUTTON_CENTER_X, "dark mode", _state["dark_mode"]
    )

    # Footer text at very bottom
    footer_text = fig.text(
        0.5, 0.008, "",
        ha="center", va="bottom",
        fontsize=8, color=current_theme()["muted"], fontfamily="monospace",
    )

    draw(fig, box_axes, chart_axes, range_axis, footer_text)

    # Minus/plus step through the ranges; labels and markers select directly.
    def _on_click(event):
        if event.inaxes is range_axis and event.xdata is not None:
            idx = range_index_for_click(event.xdata)
            _state["range"] = RANGES[idx][0]
            _state["manual_view"] = False
            draw(fig, box_axes, chart_axes, range_axis, footer_text)

    fig.canvas.mpl_connect("button_press_event", _on_click)

    def _toggle_adaptive_y(_event):
        _state["adaptive_y"] = not _state["adaptive_y"]
        style_toggle_button(
            adaptive_button, adaptive_button_background, "adaptive y", _state["adaptive_y"]
        )
        for axis, series in zip(chart_axes, SERIES):
            set_y_axis(axis, series)
        fig.canvas.draw_idle()

    adaptive_button.on_clicked(_toggle_adaptive_y)

    def _toggle_dark_mode(_event):
        _state["dark_mode"] = not _state["dark_mode"]
        if window_controls is not None:
            window_controls.apply_theme(current_theme())
        limits = [(axis.get_xlim(), axis.get_ylim()) for axis in chart_axes]
        draw(
            fig, box_axes, chart_axes, range_axis, footer_text,
            preserve_view=_state["manual_view"],
        )
        for axis, (x_limits, y_limits) in zip(chart_axes, limits):
            axis.set_xlim(x_limits)
            axis.set_ylim(y_limits)
        style_toggle_button(
            adaptive_button, adaptive_button_background, "adaptive y", _state["adaptive_y"]
        )
        style_toggle_button(
            dark_mode_button, dark_mode_button_background, "dark mode", _state["dark_mode"]
        )
        fig.canvas.draw_idle()

    dark_mode_button.on_clicked(_toggle_dark_mode)

    def _layout_controls(_event=None):
        adaptive_center = ADAPTIVE_BUTTON_CENTER_X
        dark_mode_center = DARK_MODE_BUTTON_CENTER_X
        if window_controls is not None:
            spacing = (CONTROL_BUTTON_GAP_INCHES + TOGGLE_BUTTON_WIDTH_INCHES / 2)
            dark_mode_center = max(
                dark_mode_center,
                window_controls.zoom_button_right() + spacing / fig.get_figwidth(),
            )
            adaptive_center = min(
                adaptive_center,
                window_controls.icon_buttons_left() - spacing / fig.get_figwidth(),
            )
        position_toggle_button(fig, adaptive_button.ax, adaptive_center)
        position_toggle_button(fig, dark_mode_button.ax, dark_mode_center)

        gs_outer.update(
            bottom=CONTROL_ROW_TOP + X_AXIS_LABEL_SPACE_INCHES / fig.get_figheight()
        )

    _layout_controls()
    fig.canvas.mpl_connect("resize_event", _layout_controls)

    def _remember_manual_view(event):
        if event.inaxes in chart_axes:
            toolbar = getattr(fig.canvas, "toolbar", None)
            if toolbar and toolbar.mode:
                _state["manual_view"] = True
                synchronize_x_axes(chart_axes, event.inaxes)
                for axis in chart_axes:
                    format_zoomed_xaxis(axis)
            if _state["adaptive_y"]:
                for axis, series in zip(chart_axes, SERIES):
                    set_y_axis(axis, series)
            fig.canvas.draw_idle()

    fig.canvas.mpl_connect("button_release_event", _remember_manual_view)

    def _remember_scroll_zoom(event):
        if event.inaxes in chart_axes:
            _state["manual_view"] = True
            synchronize_x_axes(chart_axes, event.inaxes)
            for axis in chart_axes:
                format_zoomed_xaxis(axis)
            if _state["adaptive_y"]:
                for axis, series in zip(chart_axes, SERIES):
                    set_y_axis(axis, series)
            fig.canvas.draw_idle()

    fig.canvas.mpl_connect("scroll_event", _remember_scroll_zoom)

    def _update(_frame):
        draw(
            fig,
            box_axes,
            chart_axes,
            range_axis,
            footer_text,
            preserve_view=_state["manual_view"],
        )

    ani = animation.FuncAnimation(
        fig, _update, interval=REFRESH_INTERVAL_MS, cache_frame_data=False
    )
    _ = ani

    plt.show()


if __name__ == "__main__":
    main()


