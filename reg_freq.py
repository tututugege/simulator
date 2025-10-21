import matplotlib.pyplot as plt

def plot_scatter_from_file(file_path, max_lines=2000, point_size=10):
    """
    从文件读取数据并绘制散点图
    :param file_path: 文件路径
    :param max_lines: 最大读取行数
    :param point_size: 散点大小（建议5-15）
    """
    x_data = []
    y_data = []
    
    # 读取文件并处理数据
    with open(file_path, 'r') as file:
        for line_num, line in enumerate(file):
            if line_num >= max_lines:
                break
                
            # 清洗数据并分割
            line = line.strip()
            if not line:
                continue
                
            # 尝试多种分隔符处理
            for sep in [',', '\t', ' ']:
                if sep in line:
                    values = line.split(sep)
                    break
            else:
                values = [line]  # 无分隔符情况
                
            # 过滤有效数值并转换
            valid_values = []
            for val in values:
                try:
                    valid_values.append(float(val))
                except ValueError:
                    continue
                    
            # 限制每行最多3个纵坐标
            valid_values = valid_values[:3]
            
            # 生成坐标数据
            x_coord = line_num
            for y_val in valid_values:
                x_data.append(x_coord)
                y_data.append(y_val)

    # 绘制散点图
    plt.figure(figsize=(12, 8))
    scatter = plt.scatter(
        x_data, y_data,
        s=point_size,          # 控制点大小
        alpha=0.7,             # 设置透明度
        edgecolors='w',        # 边缘颜色
        linewidth=0.5,         # 边缘线宽
        cmap='viridis'         # 颜色映射
    )
    
    # 添加辅助信息
    plt.xlabel('X Axis (Line Number)', fontsize=12)
    plt.ylabel('Y Axis Values', fontsize=12)
    plt.title('Multi-Value Scatter Plot', fontsize=14)
    plt.grid(True, linestyle='--', alpha=0.5)
    
    # 添加颜色条
    cbar = plt.colorbar(scatter)
    cbar.set_label('Line Number', rotation=270, labelpad=15)
    
    # 优化显示
    plt.tight_layout()
    plt.show()

# 使用示例
plot_scatter_from_file(
    file_path='log',      # 替换为你的文件路径
    max_lines=2000,            # 最大读取行数
    point_size=8               # 建议值：小点用5-10，大点用15-20
)
