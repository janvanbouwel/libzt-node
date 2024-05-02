export const checkPort = (options: object): options is { port: number } => {
  return "port" in options && typeof options.port === "number";
};
