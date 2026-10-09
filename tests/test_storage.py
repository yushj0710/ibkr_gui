import tempfile
import unittest
from pathlib import Path

from python_worker.storage import Storage


class StorageTests(unittest.TestCase):
    def test_strategy_round_trip_is_account_scoped(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            storage = Storage(Path(directory) / "desk.sqlite")
            storage.dispatch({
                "operation": "save_strategy",
                "payload": {
                    "identifier": "strategy-1",
                    "account": "DU123",
                    "label": "vertical",
                    "created_at": "2026-09-27T12:00:00Z",
                    "legs": [{"con_id": 101, "intended_ratio": 1, "opening_order_id": 4}],
                },
            })

            result = storage.dispatch({"operation": "load_strategies", "payload": {"account": "DU123"}})
            other_account = storage.dispatch({"operation": "load_strategies", "payload": {"account": "DU999"}})

            self.assertEqual(result["strategies"][0]["label"], "vertical")
            self.assertEqual(result["strategies"][0]["legs"][0]["con_id"], 101)
            self.assertEqual(other_account["strategies"], [])
            storage.close()

    def test_snapshot_and_execution_are_persisted(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            storage = Storage(Path(directory) / "desk.sqlite")
            snapshot = {
                "operation": "record_snapshot",
                "payload": {
                    "observed_at": "2026-09-27T12:00:00Z",
                    "account": "DU123",
                    "con_id": 101,
                    "market_price": 10.5,
                    "unrealized_pnl": 25.0,
                    "source": "fixture",
                },
            }
            execution = {
                "operation": "record_execution",
                "payload": {
                    "execution_id": "exec-1",
                    "observed_at": "2026-09-27T12:00:01Z",
                    "account": "DU123",
                    "con_id": 101,
                    "asset_class": "Option",
                    "action": "BUY",
                    "quantity": 1,
                    "price": 1.25,
                    "commission": 0.65,
                    "source": "fixture",
                },
            }

            self.assertEqual(storage.dispatch(snapshot), {"recorded": True})
            self.assertEqual(storage.dispatch(execution), {"recorded": True})
            storage.close()


if __name__ == "__main__":
    unittest.main()
