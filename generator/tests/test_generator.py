import argparse
import sys
import tempfile
import unittest
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[2]
GENERATOR_ROOT = REPO_ROOT / "generator"

if str(GENERATOR_ROOT) not in sys.path:
    sys.path.insert(0, str(GENERATOR_ROOT))

import parser as service_parser
from passes.consolidate import pass_consolidate
from passes.synthesize import pass_synthesize
from passes.validate import derive_service_id, pass_validate


def parse_services(service_path: Path):
    context = service_parser.ParseContext(str(service_path))
    service_parser.parse_file(context, str(service_path))
    services = context.get_services()
    for service in services:
        pass_synthesize(service)
        pass_consolidate(service)
    pass_validate(services)
    return services


class GeneratorTests(unittest.TestCase):
    def test_auto_generated_service_ids_for_new_schema(self):
        services = parse_services(REPO_ROOT / "generator/examples/test_service.gr")
        by_name = {service.get_name(): service for service in services}

        self.assertEqual(by_name["calculator"].get_id(), derive_service_id(by_name["calculator"]))
        self.assertEqual(by_name["upload"].get_id(), derive_service_id(by_name["upload"]))
        self.assertEqual(by_name["download"].get_id(), derive_service_id(by_name["download"]))
        self.assertEqual(by_name["video"].get_id(), derive_service_id(by_name["video"]))
        self.assertEqual(by_name["microphone"].get_id(), derive_service_id(by_name["microphone"]))

    def test_control_service_is_pinned_to_zero(self):
        services = parse_services(REPO_ROOT / "control.gr")
        self.assertEqual(len(services), 1)
        control = services[0]

        self.assertEqual(control.get_name(), "control")
        self.assertEqual(control.get_id(), 0)
        self.assertTrue(control.is_message())

    def test_legacy_service_keeps_explicit_id(self):
        services = parse_services(REPO_ROOT / "tests/protocols/test_service.gr")
        by_name = {service.get_name(): service for service in services}

        self.assertEqual(by_name["utils"].get_id(), 1)
        self.assertTrue(by_name["small_upload"].is_stream())
        self.assertTrue(by_name["large_download"].is_stream())

    def test_stream_services_synthesize_expected_operations(self):
        services = parse_services(REPO_ROOT / "generator/examples/test_service.gr")
        by_name = {service.get_name(): service for service in services}

        self.assertEqual(by_name["upload"].get_options()["chunk_size"], 1024 * 1024)
        self.assertEqual([func.get_name() for func in by_name["upload"].get_functions()],
                         ["open", "write_chunk", "finish", "close"])

        self.assertEqual([func.get_name() for func in by_name["download"].get_functions()],
                         ["open", "read_chunk", "close"])

        self.assertEqual([func.get_name() for func in by_name["video"].get_functions()],
                         ["open", "pause", "resume", "close"])
        self.assertEqual([evt.get_name() for evt in by_name["video"].get_events()], ["chunk"])

        self.assertEqual([func.get_name() for func in by_name["microphone"].get_functions()],
                         ["open", "write_chunk", "pause", "resume", "close"])

    def test_generation_emits_expected_files_and_ids(self):
        service_path = REPO_ROOT / "generator/examples/test_service.gr"
        with tempfile.TemporaryDirectory() as out_dir:
            args = argparse.Namespace(
                trace=False,
                service=str(service_path),
                include=None,
                out=out_dir,
                client=True,
                server=True,
                lang_c=True,
            )
            service_parser.main(args)

            out_root = Path(out_dir)
            expected_files = {
                "gracht_calculator_service.h",
                "gracht_upload_service.h",
                "gracht_download_service.h",
                "gracht_video_service.h",
                "gracht_microphone_service.h",
                "gracht_video_service_client.c",
                "gracht_upload_service_server.c",
            }
            self.assertTrue(expected_files.issubset({path.name for path in out_root.iterdir()}))

            upload_header = (out_root / "gracht_upload_service.h").read_text()
            video_header = (out_root / "gracht_video_service.h").read_text()
            upload_client = (out_root / "gracht_upload_service_client.c").read_text()
            upload_server = (out_root / "gracht_upload_service_server.c").read_text()
            calculator_client = (out_root / "gracht_calculator_service_client.c").read_text()
            video_client = (out_root / "gracht_video_service_client.c").read_text()

            self.assertIn("#define SERVICE_GRACHT_UPLOAD_ID", upload_header)
            self.assertIn("#define SERVICE_GRACHT_UPLOAD_OPEN_ID 1", upload_header)
            self.assertIn("#define SERVICE_GRACHT_UPLOAD_WRITE_CHUNK_ID 2", upload_header)
            self.assertIn("#define SERVICE_GRACHT_UPLOAD_FINISH_ID 3", upload_header)
            self.assertIn("#define SERVICE_GRACHT_UPLOAD_CLOSE_ID 4", upload_header)
            self.assertIn("#define SERVICE_GRACHT_VIDEO_EVENT_CHUNK_ID 5", video_header)
            self.assertIn("gracht_client_get_stream_buffer_sized", upload_client)
            self.assertIn("gracht_client_invoke_stream_sized", upload_client)
            self.assertIn("gracht_server_get_stream_buffer_sized", upload_server)
            self.assertIn("gracht_server_respond_stream", upload_server)
            self.assertIn("GRACHT_PROTOCOL_FLAG_STREAM", upload_server)
            self.assertIn("gracht_client_get_stream_buffer_sized", video_client)
            self.assertIn("gracht_client_invoke_stream_sized", video_client)
            self.assertIn("GRACHT_PROTOCOL_FLAG_STREAM", video_client)
            self.assertIn("gracht_client_get_buffer", calculator_client)
            self.assertIn("gracht_client_invoke", calculator_client)
            self.assertIn("GRACHT_PROTOCOL_INIT", calculator_client)


if __name__ == "__main__":
    unittest.main()