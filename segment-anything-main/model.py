import numpy as np
from segment_anything import SamPredictor, sam_model_registry


class SAMSegmenter:
    """SAM模型包装类，提供灵活的图像分割接口"""

    def __init__(self, model_type="vit_b", checkpoint_path="/raid/zhangyaming2/Projects/segment-anything-main/pretrained/sam_vit_b_01ec64.pth", device="cuda"):
        """
        初始化SAM模型

        Args:
            model_type: 模型类型，可选"vit_h", "vit_l", "vit_b"
            checkpoint_path: 模型权重文件路径
            device: 运行设备，如"cuda"或"cpu"
        """
        # 注册并加载模型
        self.sam = sam_model_registry[model_type](checkpoint=checkpoint_path)
        self.sam.to(device=device)
        self.predictor = SamPredictor(self.sam)
        self.current_image_set = False

    def set_image(self, image: np.ndarray) -> None:
        """
        设置用于分割的图像

        Args:
            image: 输入图像，numpy数组，格式为HWC uint8，像素值范围[0, 255]
        """
        # 检查图像格式
        if not isinstance(image, np.ndarray):
            raise ValueError("输入图像必须是numpy数组")
        if image.dtype != np.uint8:
            raise ValueError("图像数据类型必须是uint8")
        if len(image.shape) != 3 or image.shape[2] != 3:
            raise ValueError("图像格式必须是HWC三通道")

        self.predictor.set_image(image)
        self.current_image_set = True

    def predict(self,
                point_coords: np.ndarray = None,
                point_labels: np.ndarray = None,
                box: np.ndarray = None,
                multimask_output: bool = True) -> tuple:
        """
        使用SAM模型进行分割预测

        Args:
            point_coords: 输入提示点坐标，格式为[[x1, y1], [x2, y2], ...]
            point_labels: 对应点的标签，1表示前景，0表示背景
            box: 输入提示框，格式为[x1, y1, x2, y2]
            multimask_output: 是否输出多个可能的掩码

        Returns:
            masks: 分割掩码
            scores: 掩码置信度分数
            logits: 掩码logits
        """
        if not self.current_image_set:
            raise RuntimeError("请先调用set_image设置图像")

        # 处理空输入
        if point_coords is not None:
            point_coords = np.array(point_coords)
            if point_labels is None:
                # 默认所有点都是前景
                point_labels = np.ones(point_coords.shape[0], dtype=np.int32)
            else:
                point_labels = np.array(point_labels)

        if box is not None:
            box = np.array(box)

        # 执行预测
        masks, scores, logits = self.predictor.predict(
            point_coords=point_coords,
            point_labels=point_labels,
            box=box,
            multimask_output=multimask_output
        )

        return masks, scores, logits

    def reset(self) -> None:
        """重置当前设置的图像"""
        self.current_image_set = False