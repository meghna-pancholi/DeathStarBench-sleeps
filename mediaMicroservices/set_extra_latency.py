"""Helper script to set the extraLatencyMs value in the Helm chart for a specific service."""

import argparse
import os
import sys
import yaml
import glob

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

  if "env" not in values["container"]:
    values["container"]["env"] = []

  extra_latency_env = None
  for env_var in values["container"]["env"]:
    if env_var.get("name") == "EXTRA_LATENCY":
      extra_latency_env = env_var
      break

  if extra_latency_env:
    extra_latency_env["value"] = f"{latency_ms}ms"
  else:
    values["container"]["env"].append(
        {"name": "EXTRA_LATENCY", "value": f"{latency_ms}ms"}
    )

  try:
    with open(values_file, "w") as f:
      yaml.dump(values, f, default_flow_style=False)
    print(
        f"Successfully updated extraLatencyMs to {latency_ms} for"
        f" {service_name} in {values_file}"
    )
  except Exception as e:
    print(f"Error writing to {values_file}: {e}", file=sys.stderr)


def reset_all_services():
  """Sets extraLatencyMs to 0 for all services in the helm charts directory."""
  if not os.path.exists(HELM_CHARTS_BASE_DIR):
    print(f"Error: Helm charts directory not found at {HELM_CHARTS_BASE_DIR}", file=sys.stderr)
    return

  # Find all service directories
  service_dirs = [d for d in os.listdir(HELM_CHARTS_BASE_DIR) 
                  if os.path.isdir(os.path.join(HELM_CHARTS_BASE_DIR, d))]
  
  if not service_dirs:
    print(f"No service directories found in {HELM_CHARTS_BASE_DIR}", file=sys.stderr)
    return

  print(f"Resetting extraLatencyMs to 0 for all {len(service_dirs)} services...")
  
  for service_name in service_dirs:
    update_extra_latency(service_name, 0)
  
  print("Reset operation completed.")


if __name__ == "__main__":
  parser = argparse.ArgumentParser(
      description="Set extraLatencyMs for a service or reset all services to 0."
  )
  parser.add_argument(
      "--reset-all",
      action="store_true",
      help="Reset extraLatencyMs to 0 for all services"
  )
  parser.add_argument(
      "service_name",
      nargs="?",
      help=(
          "Name of the service directory in the charts (e.g., movie-id-service). "
          "Required unless --reset-all is used."
      ),
  )
  parser.add_argument(
      "latency_ms", 
      type=int, 
      nargs="?",
      help="The extra latency in milliseconds. Required unless --reset-all is used."
  )
  args = parser.parse_args()

  if args.reset_all:
    reset_all_services()
  else:
    if args.service_name is None or args.latency_ms is None:
      parser.error("Both service_name and latency_ms are required unless --reset-all is used.")
    update_extra_latency(args.service_name, args.latency_ms)
