#!/usr/bin/env python3

import argparse
import os
import shutil
import sys
import tempfile


ENGINE_SOURCES = {
    "kokoro": "/encoder/Cast_output_0",
    "vits": "w_ceil",
}

SAMPLES_PER_UNIT = {
    "kokoro": "600",
    "vits": "256",
}


def copy_fallback(input_path: str, output_path: str, message: str) -> int:
    print(f"[babylon] {message}", file=sys.stderr)
    if input_path != output_path:
        try:
            shutil.copyfile(input_path, output_path)
        except FileNotFoundError:
            return 0
    return 0


def set_metadata(model, key: str, value: str) -> None:
    for entry in model.metadata_props:
        if entry.key == key:
            entry.value = value
            return
    entry = model.metadata_props.add()
    entry.key = key
    entry.value = value


def main() -> int:
    parser = argparse.ArgumentParser(description="Add a stable duration output to a Babylon ONNX model.")
    parser.add_argument("--engine", required=True, choices=sorted(ENGINE_SOURCES))
    parser.add_argument("--input", required=True, dest="input_path")
    parser.add_argument("--output", required=True, dest="output_path")
    args = parser.parse_args()

    try:
        import onnx
        from onnx import helper
    except Exception as exc:
        return copy_fallback(
            args.input_path,
            args.output_path,
            f"onnx is unavailable, copying model without timing output: {exc}",
        )

    try:
        model = onnx.load(args.input_path)
    except Exception as exc:
        return copy_fallback(
            args.input_path,
            args.output_path,
            f"failed to load ONNX model, copying original: {exc}",
        )

    source_name = ENGINE_SOURCES[args.engine]

    if not any(output.name == "duration" for output in model.graph.output):
        try:
            inferred = onnx.shape_inference.infer_shapes(model)
            value_infos = {}
            for value_info in list(inferred.graph.value_info) + list(inferred.graph.input) + list(inferred.graph.output):
                value_infos[value_info.name] = value_info
            source_info = value_infos[source_name]
        except Exception as exc:
            return copy_fallback(
                args.input_path,
                args.output_path,
                f"failed to infer duration tensor {source_name}, copying original: {exc}",
            )

        dims = []
        for dim in source_info.type.tensor_type.shape.dim:
            if dim.HasField("dim_value"):
                dims.append(dim.dim_value)
            elif dim.HasField("dim_param"):
                dims.append(dim.dim_param)
            else:
                dims.append(None)

        model.graph.node.append(
            helper.make_node(
                "Identity",
                inputs=[source_name],
                outputs=["duration"],
                name="babylon_duration_identity",
            )
        )
        model.graph.output.append(
            helper.make_tensor_value_info(
                "duration",
                source_info.type.tensor_type.elem_type,
                dims,
            )
        )

    set_metadata(model, "babylon_duration_output", "duration")
    set_metadata(model, "babylon_duration_samples_per_unit", SAMPLES_PER_UNIT[args.engine])

    save_path = args.output_path
    temp_path = None
    if os.path.abspath(args.input_path) == os.path.abspath(args.output_path):
        fd, temp_path = tempfile.mkstemp(
            prefix="babylon-timing-",
            suffix=os.path.splitext(args.output_path)[1] or ".onnx",
            dir=os.path.dirname(os.path.abspath(args.output_path)) or None,
        )
        os.close(fd)
        save_path = temp_path

    try:
        onnx.checker.check_model(model)
        onnx.save(model, save_path)
        if temp_path is not None:
            os.replace(temp_path, args.output_path)
    except Exception as exc:
        if temp_path is not None:
            try:
                os.unlink(temp_path)
            except FileNotFoundError:
                pass
        return copy_fallback(
            args.input_path,
            args.output_path,
            f"failed to save patched model, copying original: {exc}",
        )

    print(f"[babylon] wrote timing-enabled model: {args.output_path}", file=sys.stderr)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
