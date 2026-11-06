from setuptools import setup, find_packages

setup(
    name="pygments-endo",
    version="0.1.0",
    description="Pygments lexer for the Endo language",
    packages=["pygments_endo"],
    package_dir={"pygments_endo": "."},
    install_requires=["pygments>=2.18"],
    entry_points={
        "pygments.lexers": [
            "endo = pygments_endo.lexer:EndoLexer",
        ],
    },
)
