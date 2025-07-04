import numpy as np
import cv2
import argparse
from model import SAMSegmenter


def main():
    parser = argparse.ArgumentParser(description='SAM模型图像分割')
    parser.add_argument('--image_path', type=str, default='/raid/zhangyaming2/WebServer/receive_imgs/temp.jpg', help='输入图像路径')
    parser.add_argument('--point_coords', type=int, nargs='+', required=True,
                        help='提示点坐标，格式为 x1 y1 x2 y2 ...')
    parser.add_argument('--output_path', type=str, default='/raid/zhangyaming2/WebServer/segment-anything-main/inference_res/seg_out.jpg', help='输出掩码路径')
    parser.add_argument('--checkpoint_path', type=str,
                        default='/raid/zhangyaming2/Projects/segment-anything-main/pretrained/sam_vit_b_01ec64.pth',
                        help='模型权重路径')
    parser.add_argument('--device', type=str, default='cuda', choices=['cuda', 'cpu'], help='运行设备')
    args = parser.parse_args()

    # 检查点坐标格式
    if len(args.point_coords) % 2 != 0:
        raise ValueError("点坐标必须是偶数个数值，格式为 x1 y1 x2 y2 ...")

    # 转换为二维数组 [[x1, y1], [x2, y2], ...]
    point_coords = np.array(args.point_coords).reshape(-1, 2).tolist()

    # 初始化分割器
    segmenter = SAMSegmenter(
        model_type="vit_b",
        checkpoint_path=args.checkpoint_path,
        device=args.device
    )

    # 加载图像
    image = cv2.imread(args.image_path)
    if image is None:
        raise FileNotFoundError(f"无法读取图像: {args.image_path}")

    image = cv2.cvtColor(image, cv2.COLOR_BGR2RGB)  # 转换为RGB格式

    # 设置图像
    segmenter.set_image(image)

    # 使用点提示进行预测
    masks, scores, logits = segmenter.predict(
        point_coords=point_coords,
        multimask_output=True
    )

    # 保存最佳掩码
    best_mask = masks[np.argmax(scores)]
    cv2.imwrite(args.output_path, best_mask.astype(np.uint8) * 255)
    print(f"分割掩码已保存至: {args.output_path}")


if __name__ == "__main__":
    main()