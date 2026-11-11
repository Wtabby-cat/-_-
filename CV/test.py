"""FOTRIC 单帧图像处理测试，对应论文第 4.2 节。

在项目根目录运行 python CV/test.py，或直接在 IDE 里运行本文件。
换图可修改下面的 IMAGE_PATH，也可运行 python CV/test.py CV/fotric_02.png。
论文图 4-7 和图 4-8 的子图分别保存到 CV/output/<图片名>/。
实测温度可直接输入 AnalyzIR 导出的逐像素摄氏温度 CSV。
"""

import argparse
from pathlib import Path

import cv2
import matplotlib.pyplot as plt
import numpy as np


IMAGE_PATH = "fotric_02.png"  # 相对本文件所在目录，也可填写绝对路径
HERE = Path(__file__).resolve().parent

# 样图采用相同机位。色标参数为（中心列，顶行，底行，最低温，最高温）。
SAMPLE_ROI = (0, 74, 460, 216)
SAMPLE_COLORBARS = {
    "fotric_01.png": (480, 34, 342, 20.1, 25.2),
    "fotric_02.png": (480, 34, 342, 26.2, 96.5),
    "fotric_03.png": (482, 34, 342, 31.5, 132.4),
}


def read_temperature(path):
    try:
        temperatures = np.loadtxt(path, delimiter=",", encoding="utf-8-sig", ndmin=2)
    except (ValueError, UnicodeError) as error:
        raise ValueError(f"{path.name} 应为仅含逐像素摄氏温度的 CSV 数值矩阵") from error
    if min(temperatures.shape) < 3 or not np.isfinite(temperatures).any():
        raise ValueError(f"{path.name} 未包含有效的二维温度场")
    return temperatures


def read_image(path):
    # 通过字节流解码，兼容 Windows 中文路径。
    encoded = np.frombuffer(path.read_bytes(), dtype=np.uint8)
    frame = cv2.imdecode(encoded, cv2.IMREAD_UNCHANGED) if encoded.size else None
    if frame is None:
        raise ValueError(f"无法读取图像 {path}，请使用 PNG、JPG、BMP 或 TIFF")
    return frame


def gray8(values):
    if values.dtype == np.uint8:
        return values
    finite = np.isfinite(values)
    if not finite.any():
        return np.zeros(values.shape, np.uint8)
    # 只归一化分割用的副本，保留测温所用的原始数值。
    clean = np.where(finite, values, values[finite].min())
    return cv2.normalize(clean, None, 0, 255, cv2.NORM_MINMAX, dtype=cv2.CV_8U)


def select_roi(preview):
    print("用鼠标框住管件，尽量避开色标和文字。按 Enter 确认，按 C 取消。")
    title = "Select CFRP tube ROI"
    try:
        return cv2.selectROI(title, preview, showCrosshair=False, printNotice=False)
    finally:
        cv2.destroyAllWindows()


def filter_components(mask):
    _, labels, stats, centers = cv2.connectedComponentsWithStats(mask, connectivity=8)
    height, width = mask.shape
    # ROI 的长轴作为管件延伸方向，短轴中心附近可以保留多个断开的区域。
    _, _, box_w, box_h, area = stats.T
    along, across = (box_w, box_h) if width >= height else (box_h, box_w)
    short_axis = min(height, width)
    center = centers[:, 1 if width >= height else 0]
    keep = (
        (area >= max(9, round(width * height * 0.0015)))
        & (np.abs(center - (short_axis - 1) / 2) <= short_axis * 0.32)
        & (along >= across * 0.65)
    )
    keep[0] = False
    return keep[labels].astype(np.uint8) * 255


def fit_edge(gradient_profiles, sample_cols, rows, sign, width):
    peaks = np.argmax(sign * gradient_profiles[rows], axis=0) + rows.start
    strong = sign * gradient_profiles[peaks, np.arange(sample_cols.size)] >= 12
    points = np.column_stack((sample_cols[strong], peaks[strong])).astype(np.float32)
    if len(points) < 8:
        return None
    vx, vy, x0, y0 = cv2.fitLine(points, cv2.DIST_HUBER, 0, 0.01, 0.01).ravel()
    if abs(vy) > 0.35 * abs(vx):
        return None
    slope = vy / vx
    errors = np.abs(points[:, 1] - (y0 + (points[:, 0] - x0) * slope))
    inliers = points[errors <= 4, 0]
    if np.count_nonzero(inliers < width / 2) < 4 or np.count_nonzero(inliers >= width / 2) < 4:
        return None
    return np.rint(y0 + (np.arange(width) - x0) * slope).astype(int)


def constrain_to_straight_tube(gray, components):
    """用近水平管件的上下边缘排除管外的高灰度区域。"""
    height, width = gray.shape
    if height < 20 or width < 40 or width <= height or not np.any(components):
        return None

    derivative = cv2.Sobel(cv2.GaussianBlur(gray, (5, 5), 0), cv2.CV_32F, 0, 1)
    positions = np.r_[np.linspace(0.04, 0.28, 10), np.linspace(0.72, 0.94, 10)]
    sample_cols = np.rint(width * positions).astype(int)
    radius = max(1, round(width * 0.01))
    profiles = np.column_stack([
        np.median(derivative[:, col - radius:col + radius + 1], axis=1)
        for col in sample_cols
    ])
    top = fit_edge(profiles, sample_cols, slice(3, height // 2), 1, width)
    bottom = fit_edge(profiles, sample_cols, slice(height // 2, height - 3), -1, width)
    if top is None or bottom is None:
        return None
    if np.any(top < 6) or np.any(bottom >= height - 6) or np.any(bottom - top < height // 5):
        return None

    rows = np.arange(height)[:, None]
    inside = (rows >= top + 2) & (rows <= bottom - 2)
    hot = components != 0
    inside_hot = inside & hot
    if np.count_nonzero(inside_hot) < 0.7 * np.count_nonzero(hot):
        return None

    return filter_components(inside_hot.astype(np.uint8) * 255)


def process_roi(preview, gray_source, roi, dark, straight_tube):
    x, y, width, height = roi
    crop = np.s_[y:y + height, x:x + width]
    gray = gray8(gray_source[crop])
    filtered = cv2.medianBlur(gray, 3)
    enhanced = cv2.equalizeHist(filtered)
    mode = cv2.THRESH_BINARY_INV if dark else cv2.THRESH_BINARY
    _, binary = cv2.threshold(enhanced, 0, 255, mode | cv2.THRESH_OTSU)
    if enhanced.min() == enhanced.max():
        binary.fill(0)
    kernel = cv2.getStructuringElement(cv2.MORPH_ELLIPSE, (3, 3))
    opened = cv2.morphologyEx(binary, cv2.MORPH_OPEN, kernel)
    closed = cv2.morphologyEx(opened, cv2.MORPH_CLOSE, kernel)
    # 有效加热区域以 Otsu 分出的热区为起点，管件边缘仅用于裁掉管外区域。
    hot_mask = filter_components(closed)
    if straight_tube and np.any(hot_mask):
        constrained = constrain_to_straight_tube(255 - filtered if dark else filtered, hot_mask)
        if constrained is not None:
            hot_mask = constrained
        else:
            print("未拟合出可靠的管件边缘，使用连通域筛选结果。")

    roi_outline = preview.copy()
    cv2.rectangle(roi_outline, (x, y), (x + width - 1, y + height - 1), (255, 230, 0), 2)
    overlay = preview.copy()
    contours, _ = cv2.findContours(hot_mask, cv2.RETR_LIST, cv2.CHAIN_APPROX_SIMPLE, offset=(x, y))
    cv2.drawContours(overlay, contours, -1, (30, 255, 30), 2)
    figures = [
        ("fig4-7a_original", preview[crop]),
        ("fig4-7b_grayscale", gray),
        ("fig4-7c_median", filtered),
        ("fig4-7d_equalized", enhanced),
        ("fig4-8a_roi", roi_outline),
        ("fig4-8b_otsu", binary),
        ("fig4-8c_heated_mask", hot_mask),
        ("fig4-8d_heated_overlay", overlay),
    ]
    return hot_mask != 0, figures


def validate_range(low, high):
    if not np.isfinite([low, high]).all() or high <= low:
        raise ValueError("标定上下限必须为有限数值且上限大于下限")


def colorbar_values(frame, pixels, colorbar):
    col, top, bottom, t_min, t_max = colorbar
    if not all(float(v).is_integer() for v in (col, top, bottom)):
        raise ValueError("色标中心列、顶行、底行须为整数像素坐标")
    col, top, bottom = int(col), int(top), int(bottom)
    height, width = frame.shape[:2]
    if not (2 <= col < width - 2 and 0 <= top < bottom < height):
        raise ValueError("色标坐标超出图像范围")
    validate_range(t_min, t_max)

    # 沿色标读出实际调色板，用 Lab 颜色距离匹配截图中的原始颜色。
    palette = np.median(frame[top:bottom + 1, col - 2:col + 3], axis=1)
    palette_lab = cv2.cvtColor(palette[:, None].astype(np.float32) / 255, cv2.COLOR_BGR2LAB)
    pixels_lab = cv2.cvtColor(pixels[:, None].astype(np.float32) / 255, cv2.COLOR_BGR2LAB)
    distances, indices = cv2.batchDistance(
        pixels_lab[:, 0], palette_lab[:, 0], cv2.CV_32F, normType=cv2.NORM_L2, K=1
    )
    scale = np.linspace(t_max, t_min, len(palette))
    return scale[indices[distances <= 12]]


def print_temperatures(preview, gray_source, temperatures, roi, mask, calibration, colorbar):
    count = np.count_nonzero(mask)
    print(f"有效加热区域像素数 {count}")
    if not count:
        print("未检出有效加热区域，请查看分割图并检查 ROI。")
        return

    x, y, width, height = roi
    crop = np.s_[y:y + height, x:x + width]
    decimals = 2
    if temperatures is not None:
        values = temperatures[crop][mask]
        values = values[np.isfinite(values)]
        source = "CSV 逐像素测温"
    elif calibration:
        g_min, g_max, t_min, t_max = calibration
        validate_range(g_min, g_max)
        validate_range(t_min, t_max)
        raw = gray_source[crop][mask].astype(np.float64)
        raw = raw[(raw >= g_min) & (raw <= g_max)]
        values = t_min + (raw - g_min) * (t_max - t_min) / (g_max - g_min)
        source = "原始灰度线性标定"
    elif colorbar:
        values = colorbar_values(preview, preview[crop][mask], colorbar)
        source = "截图色标估算（按线性色标，非相机实测）"
        decimals = 1
    else:
        print("未提供温度数据。可输入 AnalyzIR 导出的逐像素 CSV。")
        return

    print(f"温度来源 {source}，有效测温像素 {values.size}/{count}")
    if values.size:
        print(
            f"最低 {values.min():.{decimals}f} °C  "
            f"平均 {values.mean():.{decimals}f} °C  "
            f"最高 {values.max():.{decimals}f} °C"
        )
    else:
        print("掩膜内没有可用的温度数据。")


def save_figures(figures, output_dir):
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, frame in figures:
        # 直接编码像素，保持子图的原始尺寸，兼容中文路径。
        ok, encoded = cv2.imencode(".png", frame)
        if not ok:
            raise ValueError(f"无法保存 {name}.png")
        (output_dir / f"{name}.png").write_bytes(encoded.tobytes())


def show_figures(figures):
    for name, frame in figures:
        height, width = frame.shape[:2]
        dpi = max(100, width / 8, height / 6)
        figure = plt.figure(num=name, figsize=(width / dpi, height / dpi))
        axis = figure.add_axes([0, 0, 1, 1])
        axis.set_axis_off()
        if frame.ndim == 2:
            axis.imshow(frame, cmap="gray", vmin=0, vmax=255, interpolation="nearest")
        else:
            axis.imshow(cv2.cvtColor(frame, cv2.COLOR_BGR2RGB), interpolation="nearest")
    plt.show()


def main():
    parser = argparse.ArgumentParser(description="按论文第 4.2 节处理一帧 FOTRIC 热像")
    parser.add_argument("input", nargs="?", type=Path, default=HERE / IMAGE_PATH,
                        help="热像图片，或逐像素摄氏温度 CSV")
    parser.add_argument("--roi", nargs=4, type=int, metavar=("X", "Y", "W", "H"),
                        help="处理范围。不填时用鼠标框选，内置样图已预设")
    source = parser.add_mutually_exclusive_group()
    source.add_argument("--temp-csv", type=Path, help="与输入图片逐像素对齐的同一帧摄氏温度 CSV")
    source.add_argument("--colorbar", nargs=5, type=float,
                        metavar=("X", "TOP", "BOTTOM", "TMIN", "TMAX"),
                        help="线性色标的中心列、顶行、底行、最低和最高摄氏温度，用于截图估算")
    source.add_argument("--gray-calibration", nargs=4, type=float,
                        metavar=("GMIN", "GMAX", "TMIN", "TMAX"),
                        help="仅用于已知固定线性温度映射的原始单通道图像")
    parser.add_argument("--straight-tube", action="store_true",
                        help="近水平直管横跨 ROI 时，按可见上下边缘排除管外高灰度区域")
    parser.add_argument("--dark", action="store_true", help="工件灰度低于背景时反转阈值极性")
    parser.add_argument("--output-dir", type=Path, help="子图保存目录，默认为 CV/output/<图片名>")
    parser.add_argument("--no-show", action="store_true", help="保存子图，跳过显示窗口")
    args = parser.parse_args()

    try:
        path = args.input.resolve()
        sample_bar = SAMPLE_COLORBARS.get(path.name) if path.parent == HERE else None
        if path.suffix.lower() == ".csv":
            if args.temp_csv or args.colorbar or args.gray_calibration:
                raise ValueError("直接输入温度 CSV 时无需再设置温度来源")
            temperatures = read_temperature(path)
            gray_source = temperatures
            preview = cv2.applyColorMap(gray8(temperatures), cv2.COLORMAP_INFERNO)
        else:
            frame = read_image(path)
            if args.gray_calibration and frame.ndim != 2:
                raise ValueError("线性标定需要原始单通道图像")
            if frame.ndim == 2:
                gray_source = frame
                preview = cv2.cvtColor(gray8(frame), cv2.COLOR_GRAY2BGR)
            else:
                bgr = frame[:, :, :3]
                gray_source = cv2.cvtColor(bgr, cv2.COLOR_BGR2GRAY)
                preview = gray8(bgr)
            temperatures = read_temperature(args.temp_csv) if args.temp_csv else None
            if temperatures is not None:
                if temperatures.shape != preview.shape[:2]:
                    raise ValueError("温度矩阵与图片尺寸不同，请直接输入 CSV，或使用与之对齐的热像")
                gray_source = temperatures

        roi = args.roi or (SAMPLE_ROI if sample_bar else select_roi(preview))
        x, y, width, height = roi
        if not (0 <= x <= preview.shape[1] - width and 0 <= y <= preview.shape[0] - height
                and min(width, height) >= 3):
            raise ValueError("ROI 已取消、超出图像范围或小于 3x3 像素")
        print(f"输入 {path.name}，ROI (x, y, w, h) = {tuple(roi)}")
        mask, figures = process_roi(
            preview, gray_source, roi, args.dark, args.straight_tube or sample_bar is not None
        )
        print_temperatures(preview, gray_source, temperatures, roi, mask,
                           args.gray_calibration, args.colorbar or sample_bar)
        output_dir = (args.output_dir or HERE / "output" / path.stem).resolve()
        save_figures(figures, output_dir)
        print(f"论文子图已保存到 {output_dir}")
        if not args.no_show:
            show_figures(figures)
    except (OSError, ValueError, cv2.error) as error:
        parser.exit(1, f"处理失败：{error}\n")


if __name__ == "__main__":
    main()
