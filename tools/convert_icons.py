from PIL import Image

def convert_png_to_ico(source, destination):
	with Image.open(source) as image:
		image = image.convert("RGBA")
		image.save(
			destination,
			format="ICO",
			sizes=[
				(16, 16),
				(20, 20),
				(24, 24),
				(32, 32),
				(40, 40),
				(48, 48),
				(64, 64),
				(128, 128),
				(256, 256),
			]
		)
	print(f"Created {destination}")

convert_png_to_ico(
	"../resources/tray-enabled.png",
	"../resources/tray-enabled.ico")
	
convert_png_to_ico(
	"../resources/tray-disabled.png",
	"../resources/tray-disabled.ico")
