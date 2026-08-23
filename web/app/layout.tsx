import type { Metadata } from "next";
import "./globals.css";

export const metadata: Metadata = {
  title: "AntChain Workbench",
  description: "Run and inspect AntChain's C++ proof-of-work experiments."
};

export default function RootLayout({ children }: Readonly<{ children: React.ReactNode }>) {
  return <html lang="en"><body>{children}</body></html>;
}
