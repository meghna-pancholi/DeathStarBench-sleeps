"""Helper script to set the extraLatencyMs value in the Helm chart for a specific service."""

import argparse
import os
import sys
import yaml

HELM_CHARTS_BASE_DIR = "helm-chart/mediamicroservices/charts"


def update_extra_latency(service_name, latency_ms):
  """Updates the extraLatencyMs value in the values.yaml file for the given service.

  Args:
      service_name: The name of the service (e.g., movie-id-service).
      latency_ms: The value to set for extraLatencyMs.
  """
  values_file = os.path.join(HELM_CHARTS_BASE_DIR, service_name, "values.yaml")

  if not os.path.exists(values_file):
    print(
        f"Error: values.yaml not found for service {service_name} at"
        f" {values_file}",
        file=sys.stderr,
    )
    return

  try:
    with open(values_file, "r") as f:
      values = yaml.safe_load(f)
  except Exception as e:
    print(f"Error reading or parsing {values_file}: {e}", file=sys.stderr)
    return

  if "container" not in values:
    print(
        f"Error: 'container' section not found in {values_file}",
        file=sys.stderr,
    )
    return

  values["container"]["extraLatencyMs"] = latency_ms

  try:
    with open(values_file, "w") as f:
      yaml.dump(values, f, default_flow_style=False)
    print(
        f"Successfully updated extraLatencyMs to {latency_ms} for"
        f" {service_name} in {values_file}"
    )
  except Exception as e:
    print(f"Error writing to {values_file}: {e}", file=sys.stderr)


if __name__ == "__main__":
  parser = argparse.ArgumentParser(
      description="Set extraLatencyMs for a service."
  )
  parser.add_argument(
      "service_name",
      help=(
          "Name of the service directory in the charts (e.g., movie-id-service)"
      ),
  )
  parser.add_argument(
      "latency_ms", type=int, help="The extra latency in milliseconds"
  )
  args = parser.parse_args()

  update_extra_latency(args.service_name, args.latency_ms)
